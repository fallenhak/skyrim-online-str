# Skyrim Online STR Kurulum Aracı

Windows için bağımsız .NET 8 kurulum aracıdır. Var olan `Code/immersive_launcher` kaynaklarına dokunmaz; doğrulama ve mod kurulumu bittikten sonra yapılandırılmış `SkyrimTogether.exe` dosyasını `--exePath` ile çalıştırır.

## Oyun sürümü ve Stock Game kilidi

STR istemcisi Skyrim SE **1.7.104.0** sürümünü kabul eder. Launcher ve manifest yalnız `1.7.104` sürümüne izin verir. Farklı sürümlerde Türkçe hata gösterir; Steam oyununu güncellemez ve sürüm düşürmez.

Steam'deki oyun, launcher'ın yanındaki ayrı `Stock Game` klasörüne normal dosya kopyası olarak alınır. Bu klasör Steam kitaplığının içinde olamaz. Kopya, `.sos-str-stock-game.json` dosyasında `SkyrimSE.exe` ürün sürümü ve SHA-256 özetiyle kilitlenir. Kurulum ve her oyun başlangıcında ikisi de doğrulanır; dosya değiştiyse launcher çalıştırmayı durdurur. Steam'in daha sonra yaptığı güncellemeler Stock Game dosyalarına yansımaz. İlk kopya yalnız Skyrim SE 1.7.104 ise oluşturulur; yanlış sürüm için patch/downgrade uygulanmaz.

Kopya oluşturulurken `Skyrim.ccc` ve `Data` kökündeki `cc*.esl`, `cc*.esm`, `cc*.bsa` dosyaları Stock Game'den temizlenir. USSEP örneği temel oyuna uygun paketi hedefler.

## Mod sürüm tablosu

Kaynaklar 24 Eylül 2026 tarihinde kontrol edildi. Manifestteki indirme adresleri `example.invalid` yer tutucularıdır; arşiv, gerçek boyut ve SHA-256 değerleri dağıtım öncesinde doldurulmalıdır.

| Mod | Manifest sürümü | 1.7.104 durumu | Kaynak |
|---|---|---|---|
| SKSE64 | **2.3.1** (oyun kökü + Data) | Doğrulandı. İstenen 2.2.6 sürümü 1.7.104 için değil; 2.2.6, SKSE sitesinde GOG 1.6.1179 olarak listeleniyor. | [SKSE resmi indirme sayfası](https://skse.silverlock.org/) |
| Address Library (AE) | **All-in-One v13** | Doğrulandı; dosya açıklaması 1.7.104.0'a kadar tüm sürümleri kapsıyor. | [Nexus dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files) |
| SSE Display Tweaks | **0.5.25** | Doğrulandı; AE 1.7.xx dosyası listelenmiş. | [Nexus dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/34705?tab=files) |
| SSE Engine Fixes — SKSE plugin + oyun kökü DLL/preloader | **Henüz sabitlenmedi** | Yayınlanmış Nexus 7.0.21 betası **yalnız 1.7.99** içindir. Upstream ana dal/CI'de 1.7.104 derleme izi var; 1.7.104 için yayımlanmış ve doğrulanmış paket yok. Uyumlu ikili doğrulanana kadar indirme arşivini oluşturmayın. | [Nexus dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/17230?tab=files), [upstream GitHub Actions](https://github.com/aers/EngineFixesSkyrim64/actions/workflows/main.yml) |
| USSEP (CC DLC'siz temel oyun) | **4.3.9c** | Doğrulandı; resmi açıklama Skyrim 1.7.99 veya üstünü istiyor. Anniversary Upgrade DLC'si zorunlu değil. | [Nexus açıklama ve dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/266?tab=description) |
| Skyrim Souls RE | **3.1.2** | **Doğrulanmadı.** Yayımlanmış dosya runtime 1.6.1170 (Steam) ve 1.6.1179 (GOG) diyor; 1.7.104 dosyası listelenmiyor. Bu mod için güncel DLL/uyumluluk doğrulaması gerekiyor. | [Nexus dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/27859?tab=files) |
| JContainers STR | **rfortier/JContainers-rwf v4.2.13.2** | STR için 2 GB Lua bellek kısıtını kaldıran upstream sürüm doğrulandı; oyun runtime 1.7.104 uyumluluğu belirtilmemiş, **test edilmedi**. | [GitHub releases](https://github.com/rfortier/JContainers-rwf/releases) |

SKSE resmi sayfası 1.7.104 için 2.3.1 dediğinden manifest 2.2.6 yerine 2.3.1 kullanır. Engine Fixes ve Skyrim Souls RE için 1.7.104 uyumlu gerçek arşiv henüz doğrulanmamıştır; örnek manifest bu durumu sürüm alanında da işaretler. Bu iki modun yer tutucu URL'leri yayın paketi değildir.

Mod ZIP'lerinin yollarını kurulum hedefine göre düzenleyin. Örneğin Data arşivinde `Data\SKSE\Plugins\ornek.dll` ve `stripPrefix: "Data"` kullanılırsa dosya `Stock Game\Data\SKSE\Plugins\ornek.dll` konumuna yerleşir. `target: root` arşivleri oyun köküne (özellikle Engine Fixes preloader/root DLL'leri), `target: data` ise `Stock Game\Data` altına açılır. SKSE64 kök ve Data dosyaları ayrı örnek paketlerdir. Manifestte sıra numarası çakışan mod dosyalarında son yazanı belirler.

JContainers için verilen STR sürüm kaynağı [rfortier/JContainers-rwf releases](https://github.com/rfortier/JContainers-rwf/releases) sayfasıdır. USSEP tarafında temel oyun paketi kullanılır; `Stock Game` içinden Creation Club dosyaları ayrıca temizlenir.

### Manifest varlıklarını sabitleme

Varlıkları `assets` klasörüne `id.zip` adıyla koyup gerçek boyut ve SHA-256 alanlarını yazdırın:

```powershell
python .\Tools\Launcher\Scripts\update_manifest_assets.py .\Tools\Launcher\manifest.example.json --asset-dir .\assets
```

Manifest `requiredGameVersion` değeri `1.7.104` olmalıdır. Mod kaynakları ve `launcher.config.json` adresleri HTTPS olmalı; tüm ZIP'ler yayın öncesinde hedef sürümde test edilmelidir.

## Derleme, test ve publish

Visual Studio gerekmeden .NET 8 SDK ile:

```powershell
dotnet run --project .\Tools\Launcher\InstallerCore.Tests\InstallerCore.Tests.csproj
dotnet build .\Tools\Launcher\LauncherApp\LauncherApp.csproj -c Release
dotnet publish .\Tools\Launcher\LauncherApp\LauncherApp.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

Tek exe `LauncherApp\bin\Release\net8.0-windows\win-x64\publish\SkyrimOnlineSTR.Launcher.exe` altındadır. Yanına `launcher.config.json`, sunucudaki manifest URL'si ve derlenmiş `SkyrimTogether.exe` konur. `Stock Game` ilk çalıştırmada launcher klasörünün yanında oluşur.

## Sunucu ve manifest

`manifest.example.json` şema örneğidir. `launcher.config.json` içindeki `manifestUrl`, HTTPS manifest adresiniz olmalıdır. Her paket URL'si ayrı tutulduğundan mod deposu/CDN değiştirilebilir. Boyut ve SHA-256 alanları gerçek arşivlere göre doldurulmalıdır. Örnek adresler `example.invalid` olduğundan gerçek mod indirmez.

## Hata raporu

Kullanıcı kısa bir açıklama girebilir. Rapor ZIP'i launcher logunu, Skyrim/STR/SKSE/crash loglarını, `plugins.txt` ve manifest sürümünü içerir. `launcher.config.json` içindeki `errorReportEndpoint` HTTPS POST adresi, `errorReportToken` Bearer token'dır. Token için `SOS_STR_REPORT_TOKEN` ortam değişkeni config değerine tercih edilir. Endpoint veya token ayarlı değilse ZIP yerelde oluşturulur, gönderilmez.
