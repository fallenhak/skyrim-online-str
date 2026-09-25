# Sonuç

## Kök neden

- #57 hasat bildirimi ve #58 respawn bildirimi yalnızca `CellId` birebir aynı olan oyunculara gidiyordu. Etkileşim politikası aynı dış dünyada komşu grid hücrelerini de menzilde saydığı için bu oyuncular güncellemeyi kaçırabiliyordu.
- Hücreden son oyuncu çıkınca sunucu nesne bileşenini ve `IsHarvested` durumunu siliyordu. İstemci logunda 02:41:17'de hasat bildirimi, 02:43:22'de aynı nesnelerin yeniden atanıp `no longer harvested, enabling` olması bu kayıp durumu doğruluyor.
- Sabit dünya loot'u nesne atama listesine girmiyordu. Pickup kancası yalnızca oyuncu envanter değişikliğini gönderiyor; dünya referansı için sunucuda “alındı” durumu yoktu.

## Değişiklik

- Hasat ve loot yayınları artık nesne menzili ve aynı worldspace'e göre alıcı seçiyor. Hasat durumu respawn'a kadar, alınan loot durumu sunucu oturumu boyunca korunuyor.
- Kalıcı plugin referanslı armor, light, misc, weapon, ammo, key, alchemy, scroll, soul gem ve apparatus pickup'ları için sunucu doğrulamalı `TakeWorldItemRequest` / `NotifyWorldItemTaken` akışı eklendi. Ceset envanteri ve container akışları değişmedi.
- `TPTests` için menzil, tek seferlik pickup, yetki/menzil reddi, hücre boşaltma ömrü ve mesaj serileştirme testleri eklendi.

## Test, risk ve sonraki test

- Yerel test/build çalıştırılmadı; çalışma ağacında mevcut build çıktısı yok ve proje derlemesi CI'a bırakıldı. `git diff --check` temiz.
- Geçici oyuncu drop'ları makineye özgü form ID taşıdığından bu yeni akışa dahil değil. Kitap okuma davranışı da mevcut eşitleme dışı kararını koruyor.
- Sonraki iki oyunculu testte bitişik hücrelerde durup bitki toplayın; 30 dakika dolmadan hücreden çıkıp geri girince bitkinin gizli kaldığını, süre bitince geri geldiğini kontrol edin. Masadaki sabit bir item alındığında diğer oyuncunun da item'i kaybettiğini doğrulayın.
- Logda sunucuda `[World] harvest ... accepted; notified N peer(s)` ve `[World] loot ... taken; notified N peer(s)`, alıcı istemcide `harvested remotely, disabling` / `World item ... taken remotely, disabling` satırlarını arayın.
