# M01 iki oyunculu test kontrol listesi

Bu build, `integration/m01-test` dalındaki lane çalışmalarını birlikte dener. Batuhan'ın Bleak Falls Barrow örnek encounter'ı ve PR #42'deki encounter modeli temel alınmıştır.

## Sunucuyu hazırlama

- [ ] Aynı build'den iki Windows istemcisi ve Linux sunucusu kullan.
- [ ] `docs/renewable_encounters.example.txt` dosyasını `Data/renewable_encounters.txt` adıyla sunucu klasörüne kopyala.
- [ ] `config/Settings.ini` içinde `[Identity]` bölümüne `bDevTestMode=true` ekle. Ayar normalde kapalıdır; üretim sunucusunda açma.
- [ ] Sunucu portunu `10578` yap ve VDS güvenlik duvarında UDP 10578'e izin ver.
- [ ] İsteğe bağlı: `[GameServer]` bölümünde `sPassword` belirle. İki oyuncu da aynı parolayı kullanır.
- [ ] Sunucuyu başlat. Logda bir encounter, iki hücre ve hatasız slot yüklemesini doğrula.

## İki oyuncuyla bağlanma

- [ ] İki oyuncu da aynı Windows paketini ve desteklenen Skyrim sürümünü kullansın.
- [ ] Her istemcide **Connect** ekranına sunucu IP'sini ve `10578` portunu gir. Parola ayarlıysa parolayı da gir.
- [ ] Bağlantı kabul edilince **Character Select** ekranında mevcut save karakterini seç.
- [ ] İlk bağlantıda liste boşsa test modu karakteri save'den kaydeder; Character Select listesini yeniden iste.
- [ ] İki oyuncu da dünyaya girdikten sonra Bleak Falls Barrow'a birlikte git.
- [ ] Bir oyuncu çıkıp yeniden bağlanınca kayıtlı karakterin adı, ırkı, cinsiyeti, seviyesi ve konumu korunduğunu kontrol et.

## Bleak Falls Barrow encounter denemesi

- [ ] Başlangıç logunda `renewable encounters loaded encounters=1 cells=2` ve `errors=0` olduğunu doğrula.
- [ ] A ve B, `BleakFallsBarrow01` hücresine girsin; sonra ikinci bölüme (`BleakFallsBarrow02`) geçsin.
- [ ] Örnek listedeki encounter slotlarında bulunan draugr ve skeever'ları öldürün. Encounter ancak tanımlı slotların hepsi öldüğünde temizlenmiş sayılır.
- [ ] Son slot öldüğünde `[World] encounter cleared` satırının bir kez yazıldığını kontrol et.
- [ ] 60 saniyelik bekleme sırasında oyunculardan biri iki encounter hücresinden birinde kalsın. Encounter sıfırlanmamalı.
- [ ] İki oyuncu da dungeon dışına çıkınca `[World] encounter reset ... epoch=0->1` satırını kontrol et.
- [ ] Eski epoch'a ait bir test paketi sunulursa yeni epoch'taki actor durumunu değiştirmemeli. Bu paketi üretmek için özel test istemcisi gerekir.

## Bilinen sınırlar

- Encounter modeli ve ölüm/sıfırlama durumu vardır; W06 sunucu tarafı actor spawn/respawn akışı tamamlanmadı. Sıfırlama logu, düşmanların oyunda yeniden doğduğu anlamına gelmez.
- `bDevTestMode` kimliği doğrulamaz. Profil, Discord kimliği varsa ondan; yoksa karakter adından türetilir. Aynı adı kullanan oyuncular testte çakışabilir.
- Yeni test karakterinin can, magicka ve stamina değerleri 100 ile başlar.
- Envanter, zırh, perk, beceri XP'si, büyü ve shout'lar bu karakter kaydına yüklenmez.
- Kimliksiz karakter listesi isteği `IdentityNotReady` sonucu döndürür; liste ekranında hata görünmelidir.
