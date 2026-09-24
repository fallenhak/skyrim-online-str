# Skyrim Online STR Kurulum Aracı

Windows için bağımsız .NET 8 aracı. Var olan `Code/immersive_launcher` kaynaklarına dokunmaz; kurulum ve doğrulama tamamlandıktan sonra yapılandırılan `SkyrimTogether.exe` dosyasını `--exePath` ile çağırır.

## Kurulum akışı

1. Steam kayıt defteri ve `steamapps/libraryfolders.vdf` içinden Skyrim SE bulunur. Gerekirse kullanıcı oyun klasörünü elle seçer.
2. Oyuncunun Steam dosyaları aracın yanındaki `Stock Game` klasörüne kopyalanır. Oyun arşivi dağıtılmaz. Kurulum kopyası Steam kitaplığının dışındadır.
3. `SkyrimSE.exe` ürün sürümü okunur. Sürüm `1.6.1170` değilse manifestteki uygun `sostr-delta-v1` paketi indirilip uygulanır; sonuç sürümü tekrar doğrulanır.
4. `Skyrim.ccc` ve `Data` kökündeki `cc*.esl`, `cc*.esm`, `cc*.bsa` dosyaları Stock Game kopyasından silinir.
5. Mod ZIP'leri indirilir, SHA-256 ve boyutla doğrulanır, sonra sıra numarasına göre yerleştirilir. `target: root` oyun kökünü; `target: data` `Stock Game\Data` klasörünü belirtir. Böylece MO2 + Root Builder kurulumundaki kök/Data ayrımı korunur.
6. Güncellenen veya listeden çıkarılan mod dosyaları için önceki Stock Game dosyası geri yüklenir. Değişmeyen SHA-256'lı paketler tekrar indirilmez. Yarım indirmelerde HTTP Range ile sürdürme denenir.
7. Manifestteki mod `plugins` listesi oyun oturumu boyunca `%LOCALAPPDATA%\Skyrim Special Edition\plugins.txt` ve `loadorder.txt` dosyalarına uygulanır. Araç önce kullanıcının mevcut iki dosyasını yedekler; STR işlemi kapanınca aynen geri yükler. Araç beklenmedik biçimde kapanırsa sonraki açılışta yedekten kurtarır. Mod eklentilerinin adlarını paket içindeki `.esp/.esm/.esl` dosyalarına göre manifestte düzenleyin.

Mod arşivlerini sunucuda önceden düzenleyin: ZIP içindeki yollar kurulum hedefinin altına göre olmalı. Örneğin Data hedefi için `Data\SKSE\Plugins\ornek.dll` yolu ve `stripPrefix: "Data"`, dosyayı `Stock Game\Data\SKSE\Plugins\ornek.dll` konumuna koyar. `root` hedefli SKSE loader ve Engine Fixes Part 2 arşivlerinde oyun kökündeki yolları kullanın. Manifest sırası çakışan dosyalarda son yazanı belirler.

## Derleme ve test

Visual Studio gerekmeden .NET 8 SDK ile:

```powershell
dotnet run --project .\Tools\Launcher\InstallerCore.Tests\InstallerCore.Tests.csproj
dotnet build .\Tools\Launcher\LauncherApp\LauncherApp.csproj -c Release
dotnet publish .\Tools\Launcher\LauncherApp\LauncherApp.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

Tek exe `LauncherApp\bin\Release\net8.0-windows\win-x64\publish\SkyrimOnlineSTR.Launcher.exe` altında oluşur. Yanına `launcher.config.json`, sunucuda tutulan manifestten hazırlanmış manifest URL'si ve derlenmiş `SkyrimTogether.exe` dağıtılmalıdır. `Stock Game` ilk çalıştırmada bu klasörün yanında oluşur. Kurulum dosyası tek exe'dir; yapılandırma ve mevcut STR launcher dosyası yan dosya olarak kalır.

## Sunucu ve manifest

`manifest.example.json` alanları doğrudan düzenlenebilen örnektir. `launcher.config.json` içindeki `manifestUrl`, yayınladığınız HTTPS manifest adresi olmalıdır. Her paket URL'si ayrı alan olduğundan CDN/depo adresi değiştirilebilir. İndirmeler ZIP olmalı; paketler SKSE64 2.2.6'nın 1.6.1170 dosyalarını, Address Library 1.6.x, Display Tweaks, Engine Fixes 6.2.0 Part 1/2, CC'siz uygun eski USSEP, Skyrim Souls RE ve JContainers STR sürümünü içermelidir. Örnek URL'ler `example.invalid` yer tutucusudur ve gerçek mod indirmesi yapmaz.

JContainers STR paketi için verilen upstream kaynak: <https://github.com/rfortier/JContainers-rwf/releases>. Varlığı upstream'den seçip kendi paket arşivinize koyun ve manifest URL'sini kendi sunucunuza ayarlayın. USSEP için CC gerektirmeyen arşivin gerçekten hedef sürümle uyumlu sürümünü seçin. Örnek sürüm notları mod sağlayıcısının sürüm onayı yerine geçmez.

Varlık dosyalarını `assets` klasörüne `id.zip` adıyla koyduktan sonra boyut ve SHA-256 alanlarını doldurun:

```powershell
python .\Tools\Launcher\Scripts\update_manifest_assets.py .\Tools\Launcher\manifest.example.json --asset-dir .\assets
```

Patch ZIP adı `patch-{fromVersion}-to-{toVersion}.zip` biçimindedir. Oyuncuların Steam'den aldığı sürümden kendi 1.6.1170 oyun kopyanıza patch üretmek için:

```powershell
python .\Tools\Launcher\Scripts\make_delta_patch.py "D:\Steam\steamapps\common\Skyrim Special Edition" "D:\Kendi-1.6.1170-Kopyaniz" .\assets\patch-1-7-104-to-1-6-1170.zip --from-version 1.7.104
```

Bu örnekteki `1.7.104` değerini kaynak klasöründeki gerçek sürümle değiştirin. Manifestte her olası kaynak sürümü için doğrudan hedef patch kaydı bulunmalıdır; araç ara sürümler zinciri oluşturmaz. Üretici aynı konumdaki 4 KiB blokları kaynak dosyadan kopyalar, değişen blokları patch ZIP'ine ekler ve hedef dosyaların SHA-256 değerlerini ekler. `patch.json` dosyası hedef dosya yollarını, kaynak/hedef sürümlerini, kopyala/ekle işlemlerini ve silinecek yolları taşır. Yeni dosyalarda `baseSha256` boş olur; mevcut dosyalarda kaynak kopyayla eşleşmelidir. Patch uygulayıcısı hedef dosyaları önce geçici konumda üretir ve SHA-256 doğrulamasından geçirir.

## Hata raporu

Kullanıcı kısa açıklama girebilir. Araç ZIP'e launcher logunu, Skyrim/STR/SKSE/crash loglarını, `plugins.txt` ve manifest sürümünü koyar. `launcher.config.json` içindeki `errorReportEndpoint` HTTPS POST adresi, `errorReportToken` Bearer token'dır. İstek `multipart/form-data` biçiminde `file` alanı taşır. Token için `SOS_STR_REPORT_TOKEN` ortam değişkeni yapılandırma dosyasındaki değere tercih edilir. Endpoint veya token ayarlı değilse ZIP yerelde oluşturulur, gönderim yapılmaz.

## Bilinen uyumluluk konusu

Bu dalda `Code/client/main.cpp` STR istemcisinin kabul ettiği oyun sürümü olarak `1.7.104.0` bildiriyor. Bu launcher oyuncu kararına göre Skyrim SE `1.6.1170` sürümüne kilitlenir. STR istemcisi de `1.6.1170` adres/veri tabanıyla güncellenip derlenmeden mevcut ikiliyle çalışma garantisi yoktur; arayüz bu uyuşmazlığı başlatma öncesi bildirir.
