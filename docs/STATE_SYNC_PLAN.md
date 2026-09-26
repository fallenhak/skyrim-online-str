# Durum senkronu planı (26 Eylül 2026)

Durum: Burak onayladı (26.09 ~15:35). Kodlandı, gözden geçirme aşamasında. Dal: `feat/state-sync` (taban `5c893037`), PR #95.

## Uygulama özeti ve plandan sapmalar

| Adım | Commit | Ne yapıldı |
|---|---|---|
| 1a, 1c, 1d | `d6dac6e4` | Dünyaya girişte ve her reconnect'te nesne kaydı (snapshot). Dedektör farkı düzeltiyor, 30 sn'de bir yeniden deniyor. Hasat bayrağı özete eklendi. Açık eşyalar için activate gönderilmiyor. |
| 1b | `27e3960e` | Kopunca NPC'ler donuyor, giriş ekranı "yeniden bağlanılıyor" gösteriyor, istemci 1-2-4-8-10 sn aralıklarla kendisi bağlanıyor ve aynı karakteri seçiyor. Sunucu aynı hesabın eski bağlantısını kapatıyor. |
| 2 | `8d9b30b2`, `8772dd5c` | Alıcıda sapmış ceset kapatılıp settled konumda yeniden yükleniyor. Sahibin ceset envanteri kayıt oluyor, herkese dağıtılıyor. Loot başlamış cesetler dedektör döngüsünde. |
| 3 | `62c339b0` | Leveled seçime göre yeniden kurulan (conform) NPC'nin tam envanterini sahip kaydediyor, herkes onu uyguluyor. |
| 4 | `d5cc3c47` | Yakınlığa göre devir (1,5 kat ve 15 m, 5 sn), takılan sahipte (5 sn güncellemesiz) hemen devir, savaşta devir yok. Sahipsiz NPC silinmiyor, donuyor. Tetikleyici aktivasyonu sahipliği tetikleyene veriyor. |
| 5 | `faa3bf5e` | Hasat görünümü (bayrak ve model yenileme), savaşta hareket güncellemesinin atılması (sıçan gecikmesi), kilit yankısı ve kayıtsız kilit gürültüsü. |

Sapmalar:
- **Sürüm (Revision) sayacı eklenmedi.** Mesajlar tek bağlantıda sıralı ve güvenilir geliyor. Snapshot ve düzeltme, sunucunun o anki durumunu taşıyor. Sıra zaten korunuyor, ayrıca sayaca gerek kalmadı.
- **Sunucuda bekletilen assign kuyruğu gerekmedi.** Oyuncu servisi hücresini aynı olayda önce bildiriyor, nesne kaydı bir sonraki karede gidiyor.
- **Yerdeki leveled eşya hipotezi çürüdü.** Skyrim.esm taramasında tabanı leveled liste olan yerleştirilmiş referans sayısı 0 (693.333 REFR). Pot farkının nedeni bu değil. Açık kalan madde, testte ilgili eşyanın ref id'si gerekiyor.
- **NPC envanterini kayıt dosyasından kurmak yerine** sahibin gerçek envanteri kayıt yapılıyor: canlı NPC'de conform sonrası, cesette settled anında. Böylece her istemci ayrı zar atsa da tek kayıt var.
- **Sıçan gecikmesinin kanıtı** sunucu logunda bulundu: savaştaki yaratığın biriken animasyon olayları sınırı aşınca tüm hareket güncellemesi atılıyordu ("Skipped malformed server movement update for actor 23").
Kaynaklar: 26.09 13:30–13:58 testi (Burak + Bedirhan), VDS `STServerOut.log` ve `STServer-solo-20260926-1330.log`, Bedirhan istemci logu, `5c893037` kodu.

## Hedef ve kurallar

- Senkron ne olursa olsun bozulmamalı. Kopan, yeniden bağlanan, sonradan gelen ve başka bölgeden gelen herkes aynı dünyayı görmeli.
- Senkron bozuldu diye DB reset hiç gerekmemeli.
- Önemli durum sunucudadır ve DB tek doğru kaynaktır. NPC'yi yakındaki oyuncu simüle eder (3. yol, Burak 14:30).
- İş akışı: bütün maddeler yapılır, hepsi baştan gözden geçirilir, ancak ondan sonra oyun testine gidilir. Ara paket yayınlanmaz.

## Koddan çıkan bulgular

| # | Bulgu | Kanıt | Hangi şikâyeti açıklıyor |
|---|---|---|---|
| B1 | İstemci nesne kaydını (`AssignObjectsRequest`) yalnız hücre değişince gönderiyor. Bağlanınca ve yeniden bağlanınca göndermiyor. | Client `ObjectService`: yalnız `CellChangeEvent` var, `ConnectedEvent`/world sync bağlantısı yok. | Giriş hücresinde `object not registered`, mabette "steal" reddi, F2 sonrası nesne senkronunun bozulması |
| B2 | Kopunca istemci tek oyunculuya düşüyor: NPC'ler yerel olur, oyun akmaya devam eder. Yeniden bağlanma elle yapılıyor (F2). | Client `CharacterService::OnDisconnected` tüm uzak NPC'leri `SetRemote(false)` yapıp bırakıyor. | Kopmadan sonra dünyanın ayrışması |
| B3 | `[Desync]` dedektörü yalnız log yazıyor. Hasat için "client" değeri `kDisabled` bayrağından okunuyor. Oysa hasat eden oyuncunun oyunu bitkiyi kapatmıyor, yalnız "harvested" bayrağı koyuyor. Bu yüzden hasat edende yanlış alarm çıkabiliyor. | `DesyncPolicy::Compare`, `SendObjectStateReport` | Dedektör var ama düzeltmiyor; 0:8A2A6 kaydı |
| B4 | `activate: not allowed` redleri (x31, x51) masadaki açık eşyalar (open loot) için geliyor. Sunucuda bu eşyalar "alındı", istemcide hâlâ duruyor. Oyuncu E'ye bastıkça activate gidiyor. Sunucuda open loot için activate dalı yok, bu yüzden "not allowed" düşüyor. | Aynı ref'ler (0:EF047, 0:EF044, 0:4FCED...) hem `activate: not allowed` hem `[Desync] taken: server yes client no` olarak loglanmış. | x31/x51 gürültüsü; asıl sorun B1 (istemci düzeltilmiyor) |
| B5 | Uzaktan hasatta bütün bitki `Disable()` ile kapatılıyor (varsayılan fade). Normal oyunda bitki kalır, yalnız "harvested" bayrağı konur ve ucu anında gider. | Client `ApplyHarvested` | Bitkinin tamamen yavaşça fade olması |
| B6 | NPC sahipliği yalnız sahip düşünce ya da NPC'yi bırakınca el değiştiriyor. Yakınlığa göre devir yok (`AuthorityService::CanClaimActor` her zaman `false`). Uygun sahip yoksa NPC siliniyor (ceset hariç). | Server `CharacterService::TransferToNextOwner` | "Her şey bana bağlı", dungeon'ın yalnız Burak'a tepki vermesi, silinen NPC'ler |
| B7 | NPC envanteri ilk yükleyen istemcinin rastgele seçimi. Sunucu NPC envanterini plugin kaydından kurmuyor. | Server `CreateCharacter`: `inventoryComponent.Content = message...InitialInventory` | Draugr silahı ve ceset loot'u farkı |
| B8 | Ceset alıcıda yerel ragdoll ile düşüyor. Settled konum yalnız referansı ve 3D kökünü taşıyor. E'nin hedeflediği ragdoll gövdeleri yerel fizik konumunda kalıyor. | Bedirhan logunda 36 `[CorpseSync] applied`, koordinatlar doğru ama E yanlış yerde | Loot yeri kayması, "inek loot'unu bulamadı" |
| B9 | Mesajlar tek bağlantı içinde zaten güvenilir (reliable) gidiyor. Kayıp mesaj değil, asıl sorun şu: alıcı mesajı uygulayamıyor (nesne ya da aktör yüklü değil, epoch uymuyor) ve mesaj sessizce atılıyor. Kopmada ise aradaki her şey kayboluyor. | `TransportService::Send` → `Client::Send` (varsayılan reliable) | Mesaja ayrıca teslim onayı eklemek yerine düzeltme döngüsü gerekiyor |

Not (B9): Handoff'taki "kritik mesajlara teslim onayı/tekrar" maddesi bu yüzden değişiyor. Onay katmanı eklemiyoruz. Bunun yerine sunucu, her istemcinin durumunu sürekli kendi kaydıyla karşılaştırıp düzeltiyor (Adım 1c). Kaçan ya da uygulanamayan her mesaj bu döngüyle en geç birkaç saniyede kapanıyor.

## Adım 1: Durum senkronu altyapısı

### 1a. Her girişte tam hücre snapshot'ı
- İstemci `AssignObjectsRequest`'i üç durumda gönderir: world sync başladığında (ilk giriş ve reconnect), her hücre değişiminde ve sunucu "yeniden kaydol" dediğinde.
- Sunucunun cevabı zaten tam durum taşıyor (hasat, alındı, kapı, activator, kilit, envanter). İstemci bu cevabı yerel durumun yerine koyar: sunucu "alınmadı" diyorsa yerelde kapalı olan eşyayı açar, "alındı" diyorsa kapatır.
- Oyuncunun hücresi sunucuda henüz bilinmiyorsa istek "out of range" ile düşüyordu. Sunucu bu durumda isteği reddetmek yerine bekletir ve hücre bilgisi gelince işler.
- Sürüm: her nesne kaydına `Revision` sayacı eklenir (DB'de de). Snapshot ve düzeltmeler revizyon taşır, istemci eski revizyonu uygulamaz. Aynı nesne için yarışan iki mesajda sıra böylece belli olur.

### 1b. Kopmada oyunu durdur, otomatik yeniden bağlan
- Bağlantı düşünce istemci tek oyunculuya düşmez. Oyun durur, "Bağlantı koptu — yeniden bağlanılıyor" ekranı çıkar ve istemci artan aralıklarla (1, 2, 4... en fazla 10 sn) kendisi bağlanır. F2 gerekmez.
- Beklerken yerel NPC'ler "uzak" kalır, yerel AI dünyayı değiştiremez.
- Yeniden bağlanınca: karakter atanır → world sync → 1a snapshot'ı → NPC'ler yeniden atanır. Ekran snapshot uygulanınca kalkar.
- Sunucu tarafında oyuncu düşünce sahipliği hemen devredilir (bugünkü gibi). Geri gelen oyuncu sahipliği Adım 4'teki histerezisle alır.

### 1c. Dedektör düzeltme de göndersin
- İstemci 5 sn'de bir hücre özetini zaten gönderiyor. Sunucu art arda iki raporda aynı farkı görünce artık düzeltme mesajı yollar (`NotifyObjectCorrection`: nesnenin tam durumu ve revizyonu). İstemci bunu snapshot gibi uygular.
- Özet bitkinin "harvested" bayrağını da taşır. `kDisabled` ile karıştırılmaz (B3'teki yanlış alarm biter).
- Script ya da quest'in kapattığı eşyalar (`EnableParent` dışında kalanlar, ör. 0:B8717, 0:3C5A9) düzeltilmez, yalnız loglanır. Yoksa oyunun kendi durumuyla kavga ederiz.
- Düzeltme de uygulanamazsa sunucu 3. denemede nesne için "yeniden kaydol" ister.

### 1d. `activate: not allowed` gürültüsü
- İstemci open loot eşyaları için `ActivateRequest` göndermez. Onların yolu `TakeWorldItemRequest`. Sunucu açık eşyanın activate'ini sessizce yok sayar.
- Asıl sebep olan "sunucuda alınmış, istemcide duruyor" durumu 1a ve 1c ile kapanır.

Doğrulama (1): unit testler, yani `DesyncPolicy`, revizyon karşılaştırması, reconnect durum makinesi ve bekletilen assign kuyruğu. Derleme: Windows client + Linux server CI.

## Adım 2: Ceset ve E her oyuncuda cesedin üstünde
- Alıcı istemcide ceset yerel ragdoll'a bırakılmaz. Ölüm geldiğinde aktör öldürülür; settled konum gelince aktörün 3D'si o konumda yeniden kurulur (disable/enable, save'den yüklenen ceset gibi). Böylece E'nin hedeflediği gövdeler de o konumda olur.
- Settled konum gelmeden oyuncu E'ye basarsa: loot açılır ama konum gelene kadar ceset "yerleşiyor" sayılır. Bu kısa aralık bilinçli kabul.
- Sonradan gelen oyuncu cesedi zaten sunucudaki konumda, ölü olarak yükler. Bugün de böyle; korunacak.
- Ceset envanteri sunucudadır. Biri loot aldığında herkesin cesedi güncellenir (Draugr loot'unu Burak almışken Bedirhan'da durması biter). Bu kısım Adım 3 ile birlikte yapılır.
- Açık risk: poz farklı olabilir (her istemcide ceset kendi ölüm pozuna yerleşir), konum aynı olur. Gerekirse sonraki adımda kemik pozu da taşınır.

## Adım 3: Rastgele seçimleri sunucu yapsın
- NPC envanteri: sunucu NPC_ kaydından (CNTO + leveled listeler + varsayılan outfit + death item) envanteri, kapların bugün kurulduğu yolla (`PluginContainerContents`) kurar. Sabit yer seviyesi kullanılır (deleveled dünya). Giyili eşyalar outfit'ten gelir.
- Atama cevabında bu envanter hem sahibe hem izleyenlere uygulanır. Sahip istemci kendi rastgele seçimini sunucununkiyle değiştirir (leveled aktör seçiminde yapılan conform gibi).
- Yerdeki leveled eşyalar (ör. pot): önce motorun yere konan leveled eşyayı nasıl ürettiğini inceleyeceğim. Sunucu seçimi, kaydolurken istemciye gönderir. Bu madde inceleme sonucuna bağlı; bulgu çıkınca plana eklenir.
- Kalıcılık: seçimler DB'de tutulur. Sunucu yeniden başlasa da aynı draugr aynı silahla çıkar.

## Adım 4: Yakınlığa göre sahiplik
- Sunucu 1 sn'de bir sahipli NPC'lere bakar. En yakın oyuncu mevcut sahipten belirgin şekilde yakınsa (histerezis: ör. 1,5 kat ve en az 5 sn) sahiplik devredilir. Bunun için `CanClaimActor` kuralı değişir.
- Sahip takılırsa (hareket/güncelleme raporu belli süre gelmezse) erken devredilir.
- Uygun sahip yoksa NPC silinmez. Sunucuda son durumuyla donar, biri gelince o sahip olur.
- Dungeon tetikleyicileri: bir oyuncunun istemcisinde bir tetikleyici sahibi olmadığı bir NPC'yi uyandırmak isterse (restless draugr), olay sunucu üzerinden sahibe iletilir ya da sahiplik tetikleyen oyuncuya geçer. Hangisi olacağı, kodu inceledikten sonra plana eklenir.
- Savaştaki NPC'nin sahipliği savaş bitene kadar devredilmez (titreme olmasın).

## Adım 5: Küçükler
- Hasat görünümü: uzaktan hasatta `Disable()` yerine oyunun kendi "harvested" durumu kullanılır. Böylece yalnız ucu anında gider (B5).
- Sıçanların ~10 sn geç ölmesi: önce loglardan ölüm mesajının yolunu ölçeceğim (sahibin gönderimi → sunucu → alıcı). Adım 4'ten sonra da sürüyorsa ayrıca düzelteceğim.
- Lock change gürültüsü: istemci aynı durumu tekrar göndermez, sunucu güvenilmeyen kilitler için sessizce yok sayar.

## Kod dışı
- VDS sanal ağ kartı (e1000) donmaları: sağlayıcıdan virtio NIC istemek. Kod bunu çözemez. Ama 1b sayesinde böyle bir kopma artık dünyayı bozmaz.

## Sıra ve teslim
1 → 2 → 3 → 4 → 5. Her adım ayrı commit, testleri yazılmış ve CI'da yeşil. Hepsi bitince tüm fark baştan gözden geçirilir. Sonra paket ve DB reset yapılır, en son Burak + Bedirhan testi. Test listesi her şikâyeti tek tek kapsar.
