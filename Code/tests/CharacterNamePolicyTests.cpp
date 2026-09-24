#include <Services/CharacterNamePolicy.h>

#include <gtest/gtest.h>

#include <string>

TEST(CharacterNamePolicy, AcceptsThreeToTwentyFourLettersAndAllowedPunctuation)
{
    EXPECT_TRUE(CharacterNamePolicy::IsValid("Arin"));
    EXPECT_TRUE(CharacterNamePolicy::IsValid("Ava O'Kynareth"));
    EXPECT_TRUE(CharacterNamePolicy::IsValid("\xC3\x87" "a\xC4\x9F" "r\xC4\xB1" " \xC4\xB0" "\xC4\x9F" "de"));
    EXPECT_TRUE(CharacterNamePolicy::IsValid("Jean-Luc"));
    EXPECT_TRUE(CharacterNamePolicy::IsValid(std::string(24, 'A')));
}

TEST(CharacterNamePolicy, RejectsWrongLengthCharactersAndMalformedUtf8)
{
    EXPECT_FALSE(CharacterNamePolicy::IsValid("Al"));
    EXPECT_FALSE(CharacterNamePolicy::IsValid(std::string(25, 'A')));
    EXPECT_FALSE(CharacterNamePolicy::IsValid("Arin2"));
    EXPECT_FALSE(CharacterNamePolicy::IsValid("Arin_Name"));
    EXPECT_FALSE(CharacterNamePolicy::IsValid("Arin."));
    EXPECT_FALSE(CharacterNamePolicy::IsValid(std::string("A\xC3(BC", 5)));
}

TEST(CharacterNamePolicy, MakesCaseInsensitiveUniquenessKeys)
{
    EXPECT_EQ(CharacterNamePolicy::MakeUniquenessKey("S\xC3\x81" "RKA"), CharacterNamePolicy::MakeUniquenessKey("s\xC3\xA1" "rka"));
    EXPECT_EQ(CharacterNamePolicy::MakeUniquenessKey("\xC3\x87" "a\xC4\x9F" "r\xC4\xB1"), CharacterNamePolicy::MakeUniquenessKey("\xC3\xA7" "a\xC4\x9F" "r\xC4\xB1"));
}
