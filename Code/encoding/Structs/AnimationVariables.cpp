#include <Structs/AnimationVariables.h>
#include <TiltedCore/Serialization.hpp>
#include <cmath>
#include <iostream>

bool AnimationVariables::operator==(const AnimationVariables& acRhs) const noexcept
{
    return Booleans == acRhs.Booleans && Integers == acRhs.Integers && Floats == acRhs.Floats;
}

bool AnimationVariables::operator!=(const AnimationVariables& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool AnimationVariables::HasValidSizes() const noexcept
{
    return Booleans.size() <= kMaxBooleanCount && Integers.size() <= kMaxIntegerCount && Floats.size() <= kMaxFloatCount;
}

bool AnimationVariables::HasFiniteValues() const noexcept
{
    if (!HasValidSizes())
        return false;

    for (const auto value : Floats)
    {
        if (!std::isfinite(value))
            return false;
    }

    return true;
}

// std::vector<bool> implementation is unspecified, but often packed reasonably.
// The spec does not guarantee contiguous memory, though, so somewhat laborious 
// translation needed. Should be better than winding down several layers to 
// TiltedPhoques::Serialization::WriteBool, though.
//
void AnimationVariables::VectorBool_to_String(const Vector<bool>& bools, TiltedPhoques::String& chars) const
{
    chars.assign((bools.size() + 7) >> 3, 0);

    auto citer = chars.begin();
    auto biter = bools.begin();
    for (uint32_t mask = 1; biter < bools.end(); mask = 1, citer++)
        for (; mask < 0x100 && biter < bools.end(); mask <<= 1)
            *citer |= *biter++ ? mask : 0;
}

// The Vector<bool> must be the correct size when called.
//
void AnimationVariables::String_to_VectorBool(const TiltedPhoques::String& chars, Vector<bool>& bools)
{
    bools.assign(bools.size(), false);

    auto citer = chars.begin();
    auto biter = bools.begin();
    for (uint32_t mask = 1; biter < bools.end(); mask = 1, citer++)
        for (; mask < 0x100 && biter < bools.end(); mask <<= 1)
            *biter++ = (*citer & mask) ? true : false;
}


void AnimationVariables::Load(std::istream& aInput)
{
    // Booleans are bitpacked and a bit different, not guaranteed contiguous.
    TiltedPhoques::String chars((Booleans.size() + 7) >> 3, 0);

    aInput.read(reinterpret_cast<char*>(chars.data()), chars.size());
    String_to_VectorBool(chars, Booleans);
    aInput.read(reinterpret_cast<char*>(Integers.data()), Integers.size() * sizeof(uint32_t));
    aInput.read(reinterpret_cast<char*>(Floats.data()), Floats.size() * sizeof(float));
}

void AnimationVariables::Save(std::ostream& aOutput) const
{
    // Booleans bitpacked and not guaranteed contiguous.
    TiltedPhoques::String chars;
    VectorBool_to_String(Booleans, chars);
 
    aOutput.write(reinterpret_cast<const char*>(chars.data()), chars.size());
    aOutput.write(reinterpret_cast<const char*>(Integers.data()), Integers.size() * sizeof(uint32_t));
    aOutput.write(reinterpret_cast<const char*>(Floats.data()), Floats.size() * sizeof(float));
}

// Wire format description.
//
// Sends 3 VarInts, the count of Booleans, Integers and Floats, in that order. Then sends a bitstream of the
// sum of those counts. For the Booleans, these represent the bit values for the Booleans. For the Integers and
// Floats, it represents a truth table for whether the value has changed. If values HAVE changed, they follow on 
// the stream.
//
//
void AnimationVariables::GenerateDiff(const AnimationVariables& aPrevious, TiltedPhoques::Buffer::Writer& aWriter) const
{
    if (!HasValidSizes() || !aPrevious.HasValidSizes())
    {
        AnimationVariables{}.GenerateDiff(AnimationVariables{}, aWriter);
        return;
    }

    const size_t sizeChangedVector = Booleans.size() + Integers.size() + Floats.size();
    auto changedVector = Booleans;
    changedVector.reserve(sizeChangedVector);

    for (size_t i = 0; i < Integers.size(); i++)
        changedVector.push_back(aPrevious.Integers.size() != Integers.size() || aPrevious.Integers[i] != Integers[i]);
    for (size_t i = 0; i < Floats.size(); i++)
        changedVector.push_back(aPrevious.Floats.size() != Floats.size() || aPrevious.Floats[i] != Floats[i]);

    // Now serialize: VarInts Booleans.size(), Integers.size(), Floats.size(),
    // then the change table bits, then changed Integers, then changed Floats.
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Booleans.size());
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Integers.size());
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Floats.size());

    TiltedPhoques::String chars;
    VectorBool_to_String(changedVector, chars);
    TiltedPhoques::Serialization::WriteString(aWriter, chars);

    auto biter = changedVector.begin() + Booleans.size();
    for (size_t i = 0; i < Integers.size(); i++)
        if (*biter++)
            TiltedPhoques::Serialization::WriteVarInt(aWriter, Integers[i] & 0xFFFFFFFF);
    for (size_t i = 0; i < Floats.size(); i++)
        if (*biter++)
            TiltedPhoques::Serialization::WriteFloat(aWriter, Floats[i]);
}

// Reads 3 VarInts that represent the size of the Booleans, Integers and Floats.
// That's followed by a bitstream in a string of the Booleans values combined
// with a Changed? truth table for Integers and Floats.
// The Changed? table is scanned and for each true bit, the corresponsing Integer
// or Float is deserialized.
// 
bool AnimationVariables::ApplyDiff(TiltedPhoques::Buffer::Reader& aReader)
{
    const auto booleansSize = TiltedPhoques::Serialization::ReadVarInt(aReader);
    const auto integersSize = TiltedPhoques::Serialization::ReadVarInt(aReader);
    const auto floatsSize = TiltedPhoques::Serialization::ReadVarInt(aReader);

    if (booleansSize > kMaxBooleanCount || integersSize > kMaxIntegerCount || floatsSize > kMaxFloatCount)
        return false;

    if (Integers.size() != integersSize)
        Integers.assign(static_cast<size_t>(integersSize), 0);
    if (Floats.size() != floatsSize)
        Floats.assign(static_cast<size_t>(floatsSize), 0.f);

    const auto changedValueCount = booleansSize + integersSize + floatsSize;
    const auto expectedChangedBytes = static_cast<size_t>((changedValueCount + 7) / 8);
    auto chars = TiltedPhoques::Serialization::ReadString(aReader);
    if (chars.size() != expectedChangedBytes)
        return false;

    TiltedPhoques::Vector<bool> changedVector(static_cast<size_t>(changedValueCount));
    String_to_VectorBool(chars, changedVector);

    Booleans.assign(changedVector.begin(), changedVector.begin() + static_cast<size_t>(booleansSize));

    auto biter = changedVector.begin() + static_cast<size_t>(booleansSize);
    for (size_t i = 0; i < static_cast<size_t>(integersSize); i++)
        if (*biter++)
            Integers[i] = TiltedPhoques::Serialization::ReadVarInt(aReader);
    bool valid = true;
    for (size_t i = 0; i < static_cast<size_t>(floatsSize); i++)
        if (*biter++)
        {
            Floats[i] = TiltedPhoques::Serialization::ReadFloat(aReader);
            valid = valid && std::isfinite(Floats[i]);
        }

    return valid;
}
