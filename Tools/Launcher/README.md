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
| SSE Engine Fixes — SKSE plugin | **7.0.21, 1.7.104 özel derleme** | Upstream kaynak commit'i `b289e3d` 1.7.104'ü uyumlu sürüm olarak bildiriyor, Address Library ve güncel yapı bayraklarını kullanıyor. Uyumluluk görevinin Windows/MSVC derlemesi manifestte dağıtılıyor; resmi Nexus dosyası değildir. | [Nexus dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/17230?tab=files), [uyumlu kaynak commit'i](https://github.com/aers/EngineFixesSkyrim64/commit/b289e3deae71ce3915bb19c5faeeda8bf6c6a25c) |
| USSEP (CC DLC'siz temel oyun) | **4.3.9c** | Doğrulandı; resmi açıklama Skyrim 1.7.99 veya üstünü istiyor. Anniversary Upgrade DLC'si zorunlu değil. | [Nexus açıklama ve dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/266?tab=description) |
| Skyrim Souls RE | **3.1.2** | **Doğrulanmadı.** Yayımlanmış dosya runtime 1.6.1170 (Steam) ve 1.6.1179 (GOG) diyor; 1.7.104 dosyası listelenmiyor. Bu mod için güncel DLL/uyumluluk doğrulaması gerekiyor. | [Nexus dosyaları](https://www.nexusmods.com/skyrimspecialedition/mods/27859?tab=files) |
| JContainers SE | **Nexus 4.3.2** | En yeni Nexus dosyası SKSE 2.3.1 / Skyrim 1.7.104 desteğini bildiriyor; STR düzeltmesi upstream'e `ac9ec71` ile alınmış. Bu Nexus DLL'i 1.7.104'te ayrıca çalıştırılmadı. Burak'ın ayrı v4.2.13.2-rwf derlemesi Papyrus fonksiyon kaydında çöktü. | [Nexus 4.3.2](https://www.nexusmods.com/skyrimspecialedition/mods/16495), [upstream düzeltme](https://github.com/ryobg/JContainers/commit/ac9ec71), [STR duyurusu](https://github.com/rfortier/TiltedEvolution-rwf/releases) |

SKSE resmi sayfası 1.7.104 için 2.3.1 dediğinden manifest 2.3.1 kullanır. Engine Fixes için uyumluluk görevinde derlenen özel DLL kullanılır. Skyrim Souls RE'nin 3.1.2 resmi DLL'i 1.7.104'ü desteklemez ve Burak'ın etkin profilinde yoktur; dağıtım manifestine eklenmez.

Mod ZIP'lerinin yollarını kurulum hedefine göre düzenleyin. Örneğin Data arşivinde `Data\SKSE\Plugins\ornek.dll` ve `stripPrefix: "Data"` kullanılırsa dosya `Stock Game\Data\SKSE\Plugins\ornek.dll` konumuna yerleşir. `target: root` arşivleri oyun köküne (özellikle Engine Fixes preloader/root DLL'leri), `target: data` ise `Stock Game\Data` altına açılır. SKSE64 kök ve Data dosyaları ayrı örnek paketlerdir. Manifestte sıra numarası çakışan mod dosyalarında son yazanı belirler.

JContainers için verilen STR sürüm kaynağı [rfortier/JContainers-rwf releases](https://github.com/rfortier/JContainers-rwf/releases) sayfasıdır. USSEP tarafında temel oyun paketi kullanılır; `Stock Game` içinden Creation Club dosyaları ayrıca temizlenir.

### Manifest varlıklarını sabitleme

Varlıkları `assets` klasörüne `id.zip` adıyla koyup gerçek boyut ve SHA-256 alanlarını yazdırın:

```powershell
python .\Tools\Launcher\Scripts\update_manifest_assets.py .\Tools\Launcher\manifest.example.json --asset-dir .\assets
```

Manifest `requiredGameVersion` değeri `1.7.104` olmalıdır. Yeni kurulumlarda HTTPS kullanın; mevcut VDS mod deposu HTTP `:8088` üzerinden çalışıyor, dışarıdan 80/443 erişimi ve sertifikası olmadığı için geçici olarak bu gerçek URL kullanılır. ZIP'lerin boyutu ve SHA-256'sı manifestte sabitlenir; canlı oyun testi ayrıca yapılmalıdır.

## Derleme, test ve publish

Visual Studio gerekmeden .NET 8 SDK ile:

```powershell
dotnet run --project .\Tools\Launcher\InstallerCore.Tests\InstallerCore.Tests.csproj
dotnet build .\Tools\Launcher\LauncherApp\LauncherApp.csproj -c Release
dotnet publish .\Tools\Launcher\LauncherApp\LauncherApp.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

Tek exe `LauncherApp\bin\Release\net8.0-windows\win-x64\publish\SkyrimOnlineSTR.Launcher.exe` altındadır. Yanına `launcher.config.json`, sunucudaki manifest URL'si ve derlenmiş `SkyrimTogether.exe` konur. `Stock Game` ilk çalıştırmada launcher klasörünün yanında oluşur.

## Sunucu ve manifest

`manifest.example.json` şema örneğidir. Burak'ın etkin MO2 profiline göre üretilen `manifest.json` ve mod arşivleri `/srv/sos-mods/skyrim-online-str/` altında sunulur; launcher yapılandırması `http://87.76.146.253:8088/skyrim-online-str/manifest.json` adresini kullanır. Her paket URL'si ayrı tutulduğundan depo/CDN değiştirilebilir. HTTPS için VDS'te 80/443 erişimi ve otomatik sertifika yenilemesi açılmalıdır.

### Burak'ın MO2 profili ve Wabbajack yolu

Envanter `Default` profilinden 24 Eylül 2026 14:40 (İstanbul) anlık görüntüsüdür. Stock Game `SkyrimSE.exe` dosya/ürün sürümü `1.7.104.0`; `plugins.txt` etkin plugin içermiyor ve `loadorder.txt` temel oyunun altı plugin'ini içeriyor. Manifest uyumlu etkin mod dosyalarını kapsar; Stock Game ve MO2 kurulum dosyaları dağıtılmaz. Root Builder'ın etkin `Root` klasörleri SKSE dosyaları ile Engine Fixes `d3dx9_42.dll` preloader'ıdır.

| Profil bileşeni | `meta.ini` sürümü | Kaynak / Nexus mod/file kimliği | Launcher |
|---|---|---|---|
| Crash Logger SSE AE VR | 1.25.0 | Nexus 59818 / 792670 | Uyumluluk 1.7.104 için belirtilmediği için dışarıda |
| JContainers STR 1.7.104 (devre dışı) | Özel v4.2.13.2-rwf build | `modID=0`; uyumluluk görevi derlemesi | Etkinleştirilene kadar dışarıda |
| EngineFixes 1.7.104 (STR build) | Özel 7.0.21 build | `modID=0`; kaynak commit `b289e3d` | Dahil |
| Address Library for SKSE Plugins | 13.0.0 | Nexus 32444 / 795954 | Dahil |
| SSE Display Tweaks | 0.5.25.0 | Nexus 34705 / 797175 | Dahil |
| SSE Engine Fixes preloader (`Root`) | 7.0.0 | Nexus 17230 / 725261 | Dahil |
| Skyrim Script Extender (SKSE64) | 2.3.1.0 | Nexus 30379 / 795992 | Dahil |

En yeni Nexus JContainers 4.3.2 (16495 / file 800245) 1.7.104 desteğini bildiriyor ve STR düzeltmesi upstream'e birleşmiş durumda. Ancak bu Nexus DLL'i 1.7.104'te çalıştırılmadı. Burak'ın ayrı v4.2.13.2-rwf derlemesi Papyrus fonksiyon kaydı sırasında çöktüğünden özel build dağıtılmıyor. Bu MO2 anlık görüntüsünde JContainers klasörü de devre dışı; manifestte JContainers yok. Crash Logger 1.25.0 Nexus sayfası hedef runtime'ı belirtmediğinden 1.7.104 uyumu doğrulanmadı ve dağıtıma alınmadı. Skyrim Souls RE etkin profilde yoktur ve resmi 3.1.2 DLL'i hedef runtime'ı desteklemez.

| `downloads` arşivi | Bayt | Not |
|---|---:|---|
| Address Library All-in-One v13 | 6,640,552 | Nexus 32444 / 795954 |
| CrashLogger 1.25.0 | 7,201,578 | Nexus 59818 / 792670; dağıtıma alınmadı |
| Engine Fixes SKSE64 Preloader | 25,080 | Nexus 17230 / 725261; Root Builder kök dosyası |
| Mod Organizer 2.5.2 | 149,660,212 | Kurulum arşivi; dağıtıma alınmadı |
| Root Builder 5.1.1 | 957,191 | Nexus 31720 / 707262; MO2 eklentisi |
| SKSE64 Steam 2.3.1 | 952,607 | Nexus 30379 / 795992 |
| SSE Display Tweaks 0.5.25 | 187,137 | Nexus 34705 / 797175 |

Engine Fixes özel derlemesi `downloads` içinde Nexus arşivi olarak yok; launcher varlığı uyumluluk görevi derlemesinden hazırlanmıştır. JContainers özel klasörünün arşivi `downloads` içinde yok ve profil anlık görüntüsünde devre dışı.

Şimdilik Wabbajack CLI yerine MO2 profilinden sabitlenmiş manifest seçildi: launcher zaten güvenli ZIP açma, root/Data hedefi, SHA-256 ve oyun sürümü denetimini destekliyor; dört uyumlu etkin mod grubu için `.wabbajack` çözümlemesi ek kurulum bağımlılığı ve bakım getirir. Altı ZIP paketi VDS'e yüklenir (8,588,614 byte). Gelecek adım, tam MO2 profilinin yeniden kurulması ve Nexus/kurum lisanslarına uygun kaynak indirme gerekirse Wabbajack listesi/CLI desteğidir.

`Data/renewable_encounters.txt`, `integration/m01-test` dalındaki örnekten türetilir ve launcher yayın klasörüne yan dosya olarak kopyalanır; oyun `Stock Game\Data` klasörüne kurulmaz. Hata raporu endpoint'i bu dağıtımda ayarsızdır; kullanıcı raporu yerelde ZIP olarak kalır.

## Hata raporu

Kullanıcı kısa bir açıklama girebilir. Rapor ZIP'i launcher logunu, Skyrim/STR/SKSE/crash loglarını, `plugins.txt` ve manifest sürümünü içerir. `launcher.config.json` içindeki `errorReportEndpoint` HTTPS POST adresi, `errorReportToken` Bearer token'dır. Token için `SOS_STR_REPORT_TOKEN` ortam değişkeni config değerine tercih edilir. Endpoint veya token ayarlı değilse ZIP yerelde oluşturulur, gönderilmez.
