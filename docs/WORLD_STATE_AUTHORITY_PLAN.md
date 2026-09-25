# Dünya Durumu Otoritesi: Tek Tek Senkron Yamasından Genel Modele

Durum: **taslak; Burak'ın 1. ve 2. kararları işlendi** (2026-09-25). Yazan: Burak'ın Claude'u. Okuyacaklar: Burak, Batuhan ve Batuhan'ın Claude'u.
İlgili: `AUTHORITY_ARCHITECTURE.md`, `ACTOR_AUTHORITY_AUDIT.md`, GitHub #40.

## 1. Sorun

Altı iki oyunculu testte bulunan senkron hataları tek tek düzeltiliyor, her testte yenileri çıkıyor. Burak'ın sorusu: "Bunu büyük bir planla çözemez miyiz? Böyle senkronların sonu gelmeyecek."

Kısa cevap: hataların çoğu dört kök sınıftan geliyor. Bu sınıflar genel bir modelle kapatılabilir. Ama hiçbir plan senkron hatasını sıfırlamaz. Hedef, hataları "her yeni nesnede yeni bir eksik" olmaktan çıkarıp "genel hattın bir kenar durumu" haline getirmek, ve onları insanlar yerine sistemin bulmasını sağlamak.

## 2. Kanıt: bu haftanın hataları hangi sınıfa giriyor

| Sınıf | Örnek (test) | Kanıt |
|---|---|---|
| **A. Gerçeği istemci belirliyor** | Sandıkta 27 ve 7 altın, fazladan scroll (6) · draugr silahı farkı (6) | Sandık baseline'ı ilk keşfeden istemciden geliyor (`ObjectService::OnAssignObjectsRequest`). İstemci CONT'taki LVLI girdilerini eşya gibi raporluyordu (#80 `88ea5736`). |
| **B. Olay aktarılıyor, durum aktarılmıyor** | Sütun bir adım geride kalıyor (6) · 8 bit sayaç taşması (5) | Sunucu cooldown ile reddediyor ama istemci geri alınmıyor. `ActivationCount` bir sayaç, konum değil. |
| **C. Sonradan gelen oyuncuya durum eksik** | Uzaktan gelen oyuncu kol ve sütunları başlangıç halinde görebilir (kod okuması, oyunda doğrulanmadı) | Replay türü editor ID ile seçiliyor (`ActivatorReplayPolicy::Classify`). Vanilla Skyrim çalışma anında activator editor ID'lerini tutmuyor, boş ID `kNeverReplay` anlamına geliyor. Loglarda hiç `replayed` satırı yok. |
| **D. Sessiz ret / görünmeyen fark** | Kilit açma hiç senkron değildi (6), komşu hücre objeleri kayıtsızdı (6) | `TryHandleLockChange` sabit `false` alıyordu. `object not registered` retleri ancak `[Drop]` logları eklenince görüldü (#75). |

Kod da aynı şeyi söylüyor. `Code/server/Components/ObjectComponent.h`:
> "ObjectService has no authoritative static-reference type, placement, or state source. Client-discovered data remains untrusted until one exists."

Kök neden: proje, arkadaşlarla co-op için tasarlanmış, **olay aktaran** Skyrim Together mimarisinden geliyor. Biz bunu kalıcı bir dünyaya çeviriyoruz. Kalıcı dünyada nesnenin gerçek durumu bir yerde tutulmalı ve herkes onu görmeli.

## 3. Referans: SkyMP ne yapıyor (repodan doğrulandı)

`skyrim-multiplayer/skymp` (main, 2026-09):
- Sunucu ESM'leri kendisi okuyor: `libespm/` (CONT, DOOR, FLOR, ACTI, LVLI… için record sınıfları var).
- Leveled listeleri sunucu çözüyor: `skymp5-server/cpp/server_guest_lib/LeveledListUtils.cpp`, `MpObjectReference::EnsureBaseContainerAdded`.
- Kapı, sandık ve activator mantığı sunucuda: `MpObjectReference::Activate` → `ProcessActivateNormal`, DOOR/CONT/ACTI dalları.
- **Papyrus scriptleri sunucuda, kendi sanal makinelerinde koşuyor**: `papyrus-vm/`, `SendPapyrusEvent("OnActivate", …)`.

Çıkarım: SkyMP'de nesnelerin tutarlı olmasının sebebi mimari, dünyanın gerçeği sunucuda. Bedeli çok büyük: kendi Papyrus VM'leri ve tamamen farklı bir istemci yığınları var. SkyMP'ye geçmeyi ya da VM'lerini taşımayı **önermiyoruz**. Ondan iki fikri alıyoruz: dünya durumunun kaynağı sunucuda ve ESM'de, ve leveled listeleri sunucu çözüyor.
Bilmediğimiz: SkyMP'nin NPC yapay zekâsını ve scriptli dungeon'ları pratikte ne kadar desteklediği. İncelemedik.

## 3b. Diğer altyapılar ve ne alacağımız (2026-09-25 taraması)

Lisans çerçevesi: projemiz GPL-3.0 (Tilted Online). MIT ve GPL-3.0 kod doğrudan alınabilir. AGPL-3.0 kod GPLv3'ün 13. maddesiyle birleştirilebilir, ancak o zaman sunucu AGPL şartlarına girer ve bağlanan oyunculara kaynak sunulmalıdır. Repo herkese açık olduğu için bu şart karşılanıyor. **Karar (Burak, 2026-09-25): uygun yerde doğrudan alıyoruz.** Telif ve lisans notları korunur, AGPL'li dosyalar işaretlenir. Hukuki değerlendirme değildir.

| Kaynak | Lisans | Ne alıyoruz | Nasıl |
|---|---|---|---|
| **SkyMP `libespm`** | MIT | LVLI, LVLN, DOOR, FLOR, ACTI, CONT, REFR, LCTN, QUST, NAVM… okuyucuları. ECZN yok, onu biz ekleriz. | **Doğrudan**, Aşama 1a'nın temeli |
| **SkyMP `LeveledListUtils`** (skymp5-server) | AGPL-3.0 | Leveled liste çözümü | **Doğrudan al ve uyarla**, Aşama 1b. Dosya AGPL olarak işaretlenir |
| SkyMP `MpObjectReference` | AGPL-3.0 | Kapı, sandık ve activator mantığı | Okuyup fikir al. Kendi nesne modellerine sıkı bağlı |
| SkyMP `papyrus-vm` | MIT | .pex çalıştıran VM | Aşama 3'te yeniden değerlendir. Script fonksiyonları (AGPL) onların modeline bağlı |
| **TES3MP** (Morrowind) | GPL-3.0 (C++), **MIT** (CoreScripts/Lua) | **Hücre durumu modeli**: sunucu hücre başına silinen, yerleştirilen, kilitli, kapı durumu, nesne durumu, sandık, script değişkeni, tetiklenen tuzak ve aktör konum, ölüm, ekipman kayıtları tutuyor ve hücreye girişte `Load*` ile gönderiyor (`scripts/cell/base.lua`). Scriptli nesneler için hücre "actor authority" ve `synchronizedClientScriptIds` (değişkenleri senkronlanan scriptler). | **Tasarım referansı**: Aşama 2'nin `RefState` alan listesi ve Aşama 3'ün script sahibi ile değişken senkronu doğrudan buradan. Paket ayrımı (ObjectLock, DoorState, ObjectState, Container, ObjectTrap, ScriptMemberShort…) örnek alınır |
| **CommonLibSSE** (powerof3 GPL-3.0, 2026-09 aktif; CharmedBaryon NG MIT, 2024'ten beri durgun) | GPL-3.0 / MIT | Tersine mühendislikle çıkarılmış Skyrim yapıları ve fonksiyonları (InventoryChanges, leveled init, extra data…) | İstemcide ihtiyaç oldukça fonksiyon ve yapı tanımları alınır (1.7.104 uyumu her seferinde kontrol edilir) |
| po3 Tweaks | GPL-3.0 | Çalışma anında editor ID yükleme | Gerekmiyor. Sınıflandırmayı ESM base ID'leriyle yapıyoruz. Yedek seçenek |
| xEdit (TES5Edit) | MPL-2.0 | Kayıt yapısı tanımları (ECZN vb.) | Referans |
| Mutagen | GPL-3.0 (.NET) | ESM analizi | Araç tarafında (tablo üretimi) gerekirse |
| TiltedEvolution (upstream) | GPL-3.0 | Zaten tabanımız | – |

İncelenmedi ya da doğrulanmadı: kapalı kaynak SkyMP sunucuları (Keizaal vb.), NVMP, Fallout Together'ın dünya durumu yaklaşımı.

## 4. Hedef model: dört ilke

1. **Her statik referansın gerçeği sunucuda, kaynağı ESM.** İstemci "baseline" bildirmez. Türü, hücresi, konumu, kilidi ve kap içeriği sunucuda ESM'den bilinir.
2. **Her nesne için tek bir durum hattı.** Nesnenin durumu bir kayıttır, olay değil. İstemcinin eylemi bir istektir. Sunucu kabul ederse yeni durumu yayar, reddederse isteyene doğru durumu geri gönderir. Hücreye giren herkes tam durumu alır.
3. **Scriptli nesnelerde script sahibi.** Script'i bir istemci (hücrenin script sahibi) koşar. Sonucu (açık ya da kapalı, etkin ya da devre dışı, animasyon evresi) durum olarak yayılır. Yaratıklardaki sahiplik modelinin nesnelere uzantısı.
4. **Farkı sistem bulur.** İstemciler hücrelerindeki senkronlu nesnelerin durum özetini (hash) düzenli gönderir. Sunucu karşılaştırır ve farkı nesne ve alan düzeyinde loglar.

## 5. Aşamalar

### Aşama 0: Senkron farkı dedektörü (önce ölçelim)
- İstemci her ~5 sn'de bir, hücresindeki (dış mekânda yüklü gridteki) senkronlu referanslar için `(refId, alan, değer)` listesinin hash'ini ve gerekince listenin kendisini gönderir. Alanlar: disabled, open state, locked/level, harvested, taken, kap içeriği özeti.
- Sunucu kendi kaydıyla karşılaştırır ve `[Desync] ref 0:99535 inventory: server {...} client {...}` gibi satırlar yazar.
- Her test, hatta her normal oyun, kendiliğinden bir senkron raporu üretir. L2 bot bu raporu CI'da kontrol eder.
- Bu aşama hiçbir davranışı değiştirmez, risk düşük. **Sonraki aşamaların işe yarayıp yaramadığını bununla ölçeceğiz.**

### Aşama 1: ESM kaynaklı nesne kaydı (sınıf A)
- `Code/components/es_loader` bugün CONT, REFR, LVLN, NPC, RACE… okuyor. Eksik kayıtlar (**LVLI, DOOR, FLOR, ACTI, loot türleri, REFR'nin kilit (XLOC), etkin durumu ve konumu**) için **SkyMP `libespm` (MIT) alınır**. ECZN biz ekleriz.
- Sunucu açılışta statik referans tablosunu kurar: `refId → (baseType, cell, worldspace, coords, lock, container recipe)`. Ölçek, yalnız Skyrim.esm: 12.520 kap, 3.535 kapı, 11.546 flora, 17.990 activator referansı ve on binlerce loot referansı.
- `AssignObjects` artık keşif değil bir abonelik: istemci "bu hücredeyim" der, sunucu hücrenin durumunu gönderir. İstemcinin `IsDoor`/`IsContainer`/içerik bildirimleri kalkar. `HasTrustedState` her zaman doğru olur.
- **Kap içeriğini sunucu üretir**: LVLI'yi sunucu çözer (SkyMP'nin `LeveledListUtils`'i gibi). İstemci `SetInventory`'yi tam değiştirme olarak uygular. Açık karar: leveled seviye olarak kimin seviyesi kullanılacak (ilk açanın seviyesi mi, sabit bir seviye mi)?
- Batuhan'ın `fix/container-persist` (şema v7) işi buraya oturur. İçerik DB'ye yazılır, restart'ta ESM tarifinden değil DB'den gelir.

### Aşama 2: Genel referans durum hattı (sınıf B, C, D)
- Tek bir `RefState` yapısı: `{ Disabled, OpenState, Lock{IsLocked, Level}, Harvested(+respawnAt), Taken(+respawnAt), ActivatorPhase, InventoryVersion, Version }`.
- Tek mesaj ailesi: `RefStateRequest(refId, alan, beklenen Version, yeni değer)`, sunucudan `RefStateUpdate(refId, tam durum, Version)`. Ret de bir `RefStateUpdate`: isteyen doğru duruma döner. **Bugünkü `ActivateRequest`, `LockChangeRequest`, `TakeWorldItemRequest`, hasat ve kapı yollarının hepsi bu hatta toplanır.**
- Hücreye giriş: sunucu tam `RefState` listesini gönderir. Bugünkü "replay" (`ActivatorReplayPolicy`) kalkar, durum doğrudan uygulanır.
- Kalıcılık: `world_objects` tablosu genelleştirilir, `RefState`'in tamamı yazılır.
- Mevcut `ObjectInteractionPolicy`'deki menzil, yetki ve rate-limit kontrolleri korunur, tek giriş noktasına taşınır.

### Aşama 3: Scriptli nesneler (sütun, kol, puzzle, tuzak)
- Ölçek: Skyrim.esm'de 1.870 ACTI base'in **1.382'si scriptli** (%74).
- Model: her iç hücrenin (ve dış hücre bölgesinin) bir **script sahibi** olur. Seçim yaratık sahipliğindeki gibi, devir `TransferToNextOwner` benzeri. Scriptli nesneye yapılan aktivasyon yalnız sahipte scripti çalıştırır. Sahip, sonucu `RefState` olarak (open/disabled/animasyon evresi) yayar. Diğer istemciler scripti çalıştırmaz, durumu uygular.
- Sınıflandırma editor ID ile değil **ESM base ID ve script adıyla** yapılır (sunucu ESM'yi okuyor, VMAD'den script adı alınabilir).
- Kapsam dışı (ilk sürüm): quest scriptleri, diyalog. Humanoid NPC olmadığı için bunların çoğu zaten devre dışı.
- Reddedilen alternatif: sunucuda Papyrus VM (SkyMP yolu). Aylarca sürer ve Skyrim Together istemcisiyle çakışır.

### Aşama 4: Aktörler (mevcut sahiplik modeli üzerinde)
Bu belgenin çekirdeği değil. Batuhan'ın açık işleri (respawn sonrası can ve dead state, draw state, ceset konumu) `CREATURE_AUTHORITY.md` ve `COMBAT_AUTHORITY.md` çizgisinde sürer. Aşama 0'daki dedektör aktörler için de genişletilebilir: konum, can, dead state, eldeki silah.

## 6. Test yaklaşımı
- Aşama 0'ın `[Desync]` satırları her testin ilk çıktısı olur. İnsan testi yalnız his ve oynanış için kalır (Burak'ın 2026-09-25 kararı).
- L2 bot senaryoları: kap açma ve alma, kilit açma, hızlı sütun çevirme, uzaktan gelip hücreye girme, ölüm ve respawn. Her senaryonun sonunda iki istemcinin hash'i eşit olmalı.
- Aşama 1 ve 2 için birim testleri: ESM'den okunan tablo (örneğin 0:B9BBA'nın CONT'u 6 LVLI), `RefState` kabul, ret ve düzeltme akışı.

## 7. Sıra, iş bölümü, kaba süre
Süreler kaba tahmin, kesin değil.

| Sıra | İş | Öneri | Kaba süre |
|---|---|---|---|
| 1 | Aşama 0: dedektör (istemci hash, sunucu karşılaştırma, `[Desync]` log) | Burak'ın Claude'u | 2–3 gün |
| 2 | Aşama 1a: ESLoader'a LVLI/DOOR/FLOR/ACTI/loot ve REFR alanları, sunucuda statik referans tablosu | Burak'ın Claude'u | 3–5 gün |
| 3 | Aşama 1b: sunucuda leveled çözümü, kap içeriği sunucudan, `container-persist` v7 entegrasyonu | Birlikte (Batuhan: kalıcılık) | 3–5 gün |
| 4 | Aşama 2: `RefState` hattı, eski mesajların taşınması, hücreye giriş snapshot'ı | Birlikte | 1–2 hafta |
| 5 | Aşama 3: script sahibi | Sonra karar | 1–2 hafta |
| sürekli | Aşama 4 aktör işleri, L2 bot | Batuhan | – |

Bu sürede test paketleri çıkmaya devam eder. Her aşama kendi başına bir iyileştirme olarak birleşir.

## 8. Bu planın çözmediği ve riskleri
- Senkron hatası sıfırlanmaz. Kalan hatalar genel hattın kenar durumlarıdır, dedektör bunları gösterir.
- Aşama 3 kusursuz değil. Script sahibi değişirken çalışan bir script (ör. dönen sütun) yarıda kalabilir.
- Mod'lu içerik (Skyrim.esm dışı) için ESLoader'ın aynı kayıtları okuması gerekir. Load order sunucuda doğru olmalı (VDS'te `loadorder.txt` eksikliği daha önce uyarı üretti).
- Performans: onbinlerce referans sunucuda bellekte tutulur. Tahminimiz sorun olmaz, ama ölçülmedi.

## 9. Kararlar

### 9.1 Yön: tam sunucu otoritesi, her konuda (Burak, 2026-09-25)
> "Clienttan servera aktarma sistemi yerine serverdan clienta aktarma olmadı. Online oyunların çalışma mantığına çevirmemiz lazım. Senkronu tamamen kilitlemek lazım her konuda. HER konuda."

Bundan sonra:
- **Dünyanın gerçeği sunucuda.** İstemci durum bildirmez, eylem ister. Sunucu karar verir ve herkese yayar. Reddedilen eylem istemcide geri alınır. Bu kural nesneler, envanter, kilitler, activator'lar, ceset ve loot, can ve dead state, zaman ve hava için geçerli.
- **Olay aktaran yeni yama yapılmaz.** Geçici çözüm gerekirse PR'da "geçici" diye işaretlenir ve bu plandaki yerine bağlanır.
- **Dürüst sınır:** sunucuda Skyrim motoru yok. Fizik, animasyon, yapay zekâ ve vuruş tespiti gibi **simülasyon** istemcide koşmaya devam eder: aktörün sahibinde, scriptli nesnenin script sahibinde. Sunucu bu simülasyonun **sonuçlarını** doğrular ve tek gerçek olarak dağıtır. Bu, motoru olmayan her Skyrim çok oyunculu projesinin yaptığı ayrım (SkyMP de aktör hareketinde istemciye dayanıyor; Papyrus'u ise sunucuda koşuyor). "Her konuda kilitli senkron", sunucunun her durumun tek sahibi olması anlamına geliyor, simülasyonun sunucuda koşması değil.

### 9.2 Seviye: deleveled world, konuma bağlı (Burak, 2026-09-25)
> "Oyuncuların kendi seviyesine göre karşılaştığı düşman, lootları ortadan kaldırmamız lazım. Loot konum ve düşman seviyesine bağlı olmalı. Oyuncuya bağlı değil."

Önerilen uygulama (mod gerektirmez, sunucu otoritesiyle birebir uyumlu):
- Skyrim'in kendi **encounter zone (ECZN)** kayıtları konuma bağlı seviye taşıyor. Skyrim.esm'de 278 zone var ve hepsinin bir minimum seviyesi var, örneğin `BleakFallsBarrowZone` 6–20, `EmbershardMineZone` 6–10. Vanilla oyun zone seviyesini ilk girenin seviyesiyle bu aralıkta sabitliyor. Biz oyuncu seviyesini tamamen çıkarıp **zone'un sabit seviyesini** kullanıyoruz. Kural (öneri): `min` seviye, ya da `min` ile `max` arasında elle ayarlanan bir tablo.
- **Loot:** sunucu LVLI'yi (Aşama 1b) kabın bulunduğu hücrenin zone seviyesiyle çözer. Ceset loot'u da düşmanın seviyesiyle çözülür.
- **Düşman:** leveled aktörün (LVLN) şablonunu sunucu zone seviyesiyle seçer ve sahibine bildirir. Sahip istemci kendi seviyesine göre seçmez. İstemcide `Actor::GetLeveledPick` / `ExtraLeveledCreature` üzerinden seçim bugün de okunuyor, yazma tarafı eklenecek.
- ESM'de zone'u olmayan konumlar için bölge (hold) bazlı bir varsayılan tablo gerekir.
- Mod alternatifi (Requiem ya da benzeri "static level" modları): dünyayı ESP düzeyinde değiştirir, ama seviyeyi yine istemci motoru hesaplar. Sunucu otoritesiyle çakışır ve her güncellemede ESP dağıtımı gerekir. Önerilmiyor. İstenirse aday modlar ayrıca incelenebilir; isimleri ve 1.7.104 uyumlulukları doğrulanmadı.

### 9.3 Scriptli nesneler (Burak: "bilmiyorum" → öneri)
Öneri: Aşama 3'teki **script sahibi** modeli. Sebep: 9.1'e uyuyor (gerçek sunucuda, simülasyon tek sahipte) ve sunucuda Papyrus VM'den (SkyMP yolu) çok daha küçük bir iş. Aşama 2 bitince yeniden değerlendirilir.

### 9.4 İş bölümü (Burak: "bilmiyorum" → öneri)
- **Burak'ın Claude'u:** Aşama 0 (dedektör), Aşama 1a (ESLoader'a LVLI/DOOR/FLOR/ACTI/ECZN ve statik referans tablosu), Aşama 1b (sunucuda leveled çözümü ve zone seviyesi), Aşama 2'nin sunucu tarafı.
- **Batuhan:** `container-persist` v7 (1b'ye bağlanır), Aşama 4 aktör işleri (respawn/dead state, ceset, draw state), L2 bot ve dedektörün CI'da kullanılması, leveled aktör seçiminin istemci yazma tarafı.
- Aşama 2'nin istemci tarafı ve Aşama 3: 1b bittikten sonra paylaşılır.
- Batuhan'ın itirazı varsa #40'ta konuşulur.
