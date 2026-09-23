# Raporlar — batudev0

> Bu dosyayı yalnızca **batudev0** (Batuhan + Claude) yazar. fallenhak okur, dokunmaz.
> Karşı tarafın raporu: [fallenhak.md](fallenhak.md)
>
> Yeni rapor en üste eklenir. Biçim: `## YYYY-MM-DD — başlık`, ardından ne yapıldı,
> hangi dal/PR, sıradaki adım ve karşı taraftan beklenen (varsa).

## 2026-09-23 — Başlangıç: M01-WORLD (W01–W04) hattını üstleniyoruz

**Durum tespiti**
- Branch ve commit taraması yapıldı. Aktif paralel hatlar (Combat, Authority,
  Population/ESLoader, Character UI) ve `feature/*`, `hardening/*`, `refactor/*` dallarına
  **dokunulmayacak**, push edilmeyecek.
- `.orchestrator/roadmap.json` içinde `M01-WORLD` hattı `branch: null`,
  `BLOCKED_DEPENDENCY` (Combat + Population) durumunda — sahipsiz.

**Üstlendiğimiz iş**
- **W01** — stabil mantıksal spawn kimliği + renewable encounter grup modeli (`depends_on: []`).
- **W02** — sunucuya ait dungeon/encounter popülasyon durumu, alive/dead üyeliği.
- **W04** — yapılandırılabilir reset uygunluğu + cooldown politikası.
- W03 (güvenli cleared-state tespiti) Combat'ın "gerçek ölüm" sinyaline bağlı; bunu ince bir
  arayüz arkasında tutacağız, Combat birleşince tek adaptörle bağlanacak.

**Çalışma şekli**
- Dal: `human/batuhan/world-encounters`. İş bitince commit + push + PR; **merge etmiyoruz**.
- Saf sunucu tarafı mantık + unit testler; Combat/Population koduna değişiklik yok.

**Karşı taraftan rica**
- Orkestratörün W01–W04'ü ayrıca açmaması için roadmap'te `M01-WORLD` için sahip/dal notu
  düşülebilir mi?
- Hangi dal lifecycle/incarnation için kararlı taban kabul ediliyor
  (`hardening/longhaul-creature-combat` mı, `parallel/combat-foundations` mı)?
