# M01 iki oyunculu test kontrol listesi

Bu build, `integration/m01-test` dalındaki lane çalışmalarını birlikte dener. Batuhan'ın Bleak Falls Barrow örnek encounter'ı ve PR #42'deki encounter modeli temel alınmıştır.

## İstemci kurulumu

- [ ] Skyrim Special Edition **1.7.104** (Steam) kurulu olsun.
- [ ] Modları elle kurma. Launcher şunları kurar: SKSE 2.3.1, Address Library, Engine Fixes, Display Tweaks, Crash Logger.
- [ ] Launcher Steam kurulumuna dokunmaz. Ayrı bir Stock Game kopyası üzerinde çalışır ve CC dosyalarını o kopyadan temizler.
- [ ] Çökme olursa `Documents/My Games/Skyrim Special Edition/SKSE/crash-*.log` dosyasını ya da launcher'ın hata raporu ZIP'ini #40'a ekle.

## Sunucuyu hazırlama

- [ ] Aynı build'den iki Windows istemcisi ve Linux sunucusu kullan.
- [ ] `docs/renewable_encounters.example.txt` dosyasını `Data/renewable_encounters.txt` adıyla sunucu klasörüne kopyala.
- [ ] `config/STServer.ini` içinde `[Identity]` `bDevTestMode` **kapalı** (varsayılan) olmalı. Kimlik Discord girişinden gelir; dev modu doğrulanmamış kimlik bağlar ve save'den karakter tohumlar, save'siz akışla çelişir.
- [ ] Sunucu portunu `10578` yap ve VDS güvenlik duvarında UDP 10578'e izin ver.
- [ ] İsteğe bağlı: `[GameServer]` bölümünde `sPassword` belirle. İki oyuncu da aynı parolayı kullanır.
- [ ] Sunucuyu başlat, sonra açılışı doğrula:
  `python3 Tools/Scripts/server_startup_check.py --root /var/lib/sos-server`
  Port 10578, `Data/renewable_encounters.txt` ve `errors=0` kontrol edilir. Hepsi geçerse çıkış kodu 0, bir kontrol düşerse 1 olur. Diğer lane'lerin kabul satırları `--require '<regex>'` ile eklenebilir.

## Giriş ve karakter oluşturma

Ayrıntılı elle kontrol listesi: `docs/CHARACTER_ENTRY_NO_SAVE.md`.

- [ ] Launcher'dan **Discord ile giriş** yap, sonra **Oyna**'ya bas. Oyun açılınca sunucuya otomatik bağlanır, IP/port girilmez.
- [ ] Karakter ekranında 3 slot görünmeli: slot 0 açık, slot 1 ve 2 kilitli.
- [ ] Yeni karakter oluştur (ad). Helgen sahnesi / `MQ101` başlamadan karakter **Whiterun, Kynareth Mabedi**'nde doğmalı. Merdiven, duvar ya da zemin altında olmamalı.
- [ ] RaceMenu açılmalı. Kapatınca dünyaya geçilmeli, seçilen ırk ve cinsiyet karakter listesinde kalmalı.
- [ ] Save yüklenmez: oyun klasöründe yeni `.ess` oluşmamalı.
- [ ] İki oyuncu da dünyaya girdikten sonra birbirini görmeli, sonra Bleak Falls Barrow'a birlikte gitmeli.
- [ ] Bir oyuncu çıkıp yeniden girince RaceMenu açılmamalı. Karakter kayıtlı son konumda başlamalı; adı, ırkı, cinsiyeti ve seviyesi korunmalı.

## Bleak Falls Barrow encounter denemesi

- [ ] Başlangıç logunda `renewable encounters loaded encounters=1 cells=1 slots=38` ve `errors=0` olduğunu doğrula.
- [ ] A ve B, `BleakFallsBarrow01` hücresine girsin; sonra ikinci bölüme (`BleakFallsBarrow02`) geçsin.
- [ ] Örnek listedeki encounter slotlarında bulunan draugr ve skeever'ları öldürün. Encounter ancak tanımlı slotların hepsi öldüğünde temizlenmiş sayılır.
- [ ] Son slot öldüğünde `[World] encounter cleared` satırının bir kez yazıldığını kontrol et.
- [ ] 60 saniyelik bekleme sırasında oyunculardan biri iki encounter hücresinden birinde kalsın. Encounter sıfırlanmamalı.
- [ ] İki oyuncu da dungeon dışına çıkınca `[World] encounter reset ... epoch=0->1` satırını kontrol et.
- [ ] Eski epoch'a ait bir test paketi sunulursa yeni epoch'taki actor durumunu değiştirmemeli. Bu paketi üretmek için özel test istemcisi gerekir.

## Test sonrası

- [ ] Sunucu log'unu encounter bazında özetle:
  `python3 Tools/Scripts/encounter_log_report.py /var/lib/sos-server/logs/STServerOut.log`
  Her encounter için cleared / blocked / reset sırasını, spawn ve snapshot satırlarını listeler. Sorun bulunmazsa çıkış kodu 0, bulunursa 1 olur. Çıktıyı #40'a ekle.

## Bilinen sınırlar

- Encounter modeli ve ölüm/sıfırlama durumu vardır; W06 sunucu tarafı actor spawn/respawn akışı tamamlanmadı. Sıfırlama logu, düşmanların oyunda yeniden doğduğu anlamına gelmez.
- `bDevTestMode` kimliği doğrulamaz. Profil, Discord kimliği varsa ondan; yoksa karakter adından türetilir. Aynı adı kullanan oyuncular testte çakışabilir.
- Yeni test karakterinin can, magicka ve stamina değerleri 100 ile başlar.
- Envanter, zırh, perk, beceri XP'si, büyü ve shout'lar bu karakter kaydına yüklenmez.
- Kimliksiz karakter listesi isteği `IdentityNotReady` sonucu döndürür; liste ekranında hata görünmelidir.
