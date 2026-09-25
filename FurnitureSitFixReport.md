# Oturma senkronu düzeltmesi — 25 Eylül 2026

- **Kök neden:** Uzak animasyonlar 300 ms gecikmeyle oynatılıyor. Sandalye giriş action'ları yalnızca geç katılım tekrarında anlık karşılıklarına çevriliyordu; canlı akışta ham giriş olayı geç oynatılınca `ForceAction` başarısız olabiliyor ve sonuç kontrol edilmeden kuyruktan siliniyordu. Önceki istemci günlüğünde oturma olayını gösteren kayıt yoktu.
- **Değişiklik:** Canlı sandalye/tabure/oturma girişleri anlık action'a çevriliyor ve kısa, sınırlı tekrar uygulanıyor. Sunucu `TargetId` başına tek oyuncuyu rezerve ediyor; dolu koltuğu reddedip ikinci istemciyi ayağa kaldırıyor ve sunucudaki son konumuna döndürüyor. TPTests’e politika, animasyon dönüşümü ve mesaj kodlama testleri eklendi.
- **Test:** Yerel derleme ve test çalıştırılmadı; CI doğrulayacak. `git diff --check` temiz.
- **Risk:** Eylemin `TargetId`'si furniture referansını göstermeli; sıfırsa sunucu uyarı kaydı bırakır ve kilit koyamaz. `isInFurniture` bayrak konumu vanilla oyuncu animasyon grafiğine bağlı.
- **Sonraki test:** İki oyuncuyla `Reserved furniture`, `Denied seat action`, `Replayed seat action` ve `Furniture use denied` kayıtlarını karşılaştırın. Aynı koltuğun hedef kimliğinin eşleştiğini, ilk oyuncuda başarılı replay sonucu ve ikinci oyuncuda pozisyon düzeltmesi olduğunu doğrulayın.
