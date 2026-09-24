# Save dosyasız karakter girişi

## Seçilen yol

Ana menüde `New Game` seçilmez ve bir `.ess` dosyası açılmaz. Snapshot geldiğinde istemci native Skyrim konsolunu açıp `coc WhiterunTempleofKynareth` komutunu çalıştırır. Hücre yüklenene kadar loading ekranı arkasında bekler; sonra sunucu snapshot'ındaki ad, ırk, cinsiyet, seviye, cell ve konumu oyuncuya uygular ve mevcut `CharacterReadyRequest` → yerel oyuncu ataması → `NotifyCharacterEnteredWorld` akışına devam eder.

SkyMP'nin Skyrim Platform örneği ana menüden konsol açıp `coc riverwood` ile oyuna girmeyi gösteriyor. Whiterun Kynareth Mabedi interior hücresinin EditorID'si `WhiterunTempleofKynareth`, Skyrim.esm FormID'si `000165A7`. Konsol komutunun referans uygulaması CommonLib'de `Console::ExecuteCommand(const char*)` olarak tanımlı; bu kod da Skyrim'in `FxDelegateArgs` çağrı yolunu kullanıyor. Bağlantılar:

- [SkyMP plugin örneği: ana menüden `coc riverwood`](https://github.com/skyrim-multiplayer/skymp/blob/main/skyrim-platform/tools/plugin-example/README.md)
- [CommonLib: native konsol komut çağrısı](https://github.com/libxse/commonlibsse/blob/main/src/RE/C/Console.cpp)
- [WhiterunTempleofKynareth hücre kaydı ve FormID](https://steamcommunity.com/app/489830/discussions/0/4339861173664120302/?l=german)

Başlangıç konumu sabit tahmin koordinatları değildir: COC tamamlandıktan sonra istemci hücrenin vanilla COC marker konumunu `TESObjectCELL::GetCOCPlacementInfo` ile alır. Yeni karakterde bu konum `CharacterReadyRequest` üzerinden gönderilir ve sunucu assignment'tan önce DB'ye yazar. Bundan sonraki normal runtime save-back, Skyrim save dosyasına değil owner-scoped karakter DB kaydına cell/konum/vital değerleri yazar.

Yeni karakter sunucuya `NeedsRaceMenu=true` ile kaydedilir. Oyuncu atanıp dünyaya girdikten sonra istemci overlay'i kapatır, native `showracemenu` komutunu açar ve `RaceSex Menu` kapanana kadar bekler. Seçilen ırk ve cinsiyet sunucuya gönderilir; sunucu bu V1 alanlarını günceller ve `NeedsRaceMenu` değerini kapatır. Mevcut karakterde RaceMenu adımı çalışmaz. `MQ101`/Helgen quest'i başlatan bir çağrı veya ek `.esp` yoktur.

## Elle doğrulama

1. Oyun ilk açılışta ana menüdeyken launcher üzerinden bağlan; karakter listesi yerine giriş/loading ekranı görünmeli.
2. Yeni karakter oluştur; slot 0 açılmalı, slot 1 ve 2 kilitli görünmeli. Ad için 2, 25, rakam/noktalama, Türkçe harf, mevcut ad ve boş slot durumlarını dene.
3. Oluşturduktan sonra Helgen sahnesi / `MQ101` başlamadan karakter Kynareth Mabedi içinde doğmalı. Konum merdiven, duvar veya zeminin altında olmamalı.
4. RaceMenu açılmalı; kapatınca oyun dünyasına geçilmeli ve karakter listesinde seçilen ırk/cinsiyet kalmalı.
5. Oyundan çıkıp tekrar gir; mevcut karakter RaceMenu açmadan kayıtlı son konumda başlamalı. Oyun klasöründe yeni bir `.ess` oluşmamalı.
6. İkinci karakter oluşturma denemesi dolu slot sonucu vermeli; kilitli slot için sunucudan `slotLocked` sonucu dönmeli.

Başlık ekranında COC'nin bazı modların `OnPlayerLoadGame` benzeri başlangıç davranışlarını atlayabildiği raporlanıyor; burada özellikle normal New Game/Helgen açılışını istemediğimiz için bu yol seçildi. Oyundaki gerçek runtime doğrulaması bu nedenle zorunlu.
