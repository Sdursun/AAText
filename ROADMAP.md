# AAText – Yol haritası

Bu dosya, 0.9 sürümünden sonra yapılabilecek işleri ve önceliklerini
listeler. Kesin bir takvim değildir; sıra, kullanıcı geri bildirimlerine
göre değişebilir.

**Durum (0.9):** Ana plandaki 1–5. ve 7. aşamalar tamamlandı: iskelet,
RTG algılama, FreeType ile yumuşatılmış yazı, önbellek/kara liste/ayar
dosyası, gerçek ölçü modu, yayın. Ek olarak `.otag` dosyalarıyla otomatik
TrueType algılama eklendi. **6. aşama (AGA ekranlar) yapılmadı.**

---

## Önerilen sıra

1. **Geri bildirim toplamak.** Birkaç gün kullanıcıların bildirdiği
   hatalara bakmak. Gerçek hatalar her zaman yeni özelliklerden önce
   gelir.
2. **Hinting seçeneği** (küçük iş, hemen fark edilir); **OpenType/CFF**
   ancak freetype2.library ile `.otf` kurulabildiği doğrulanırsa (madde 2).
3. **Ayar programı (GUI).** Kullanıcı sayısı arttıkça "metin dosyası
   düzenleyin" demek zorlaşır.
4. Diğerleri talebe göre. AGA ancak istek gelirse.

---

## Kullanıcıların doğrudan fark edeceği işler

### 1. Ayar programı (ReAction GUI) — orta
- Gamma, `real`, önbellek boyutu, kara liste ve `offscreen` gibi
  ayarları bir pencereden değiştirmek.
- Değişikliğin anında önizlenmesi; Use/Save/Cancel düğmeleri (Prefs
  programlarının standart davranışı).
- `ENV:` / `ENVARC:AAText.prefs` dosyasını yazar. Çalışan AAText'e
  ayarları yeniden okumasını söyler; bunun için AAText'te küçük bir
  mesaj arayüzü gerekir.

### 2. OpenType/CFF desteği (`.otf`) — kolay, ama bir ön koşula bağlı
- AAText sadece sistemin açtığı fontları çizer. Bir `.otf`'ten Amiga fontu
  (ve `.otag`) üretecek bir **motor** olmadan AAText'e CFF eklemek işe
  yaramaz.
- Mevcut kurulumdaki motor **ttf.library** (`.otag` içinde motor adı
  `ttf`): sadece TrueType, OpenType/CFF yok.
- OS 3 için **freetype2.library** var (Aminet `util/libs/freetype2_lib`,
  AROS'un motoru, FTManager ile). OpenType/CFF desteği teorik olarak var
  ama **doğrulanmadı** (eski bir FreeType sürümü olabilir). Önce bunun
  `.otf` kurup kuramadığı denenmeli.
- Kurabiliyorsa AAText tarafı kolay: FreeType'a `cff`, `psaux`,
  `pshinter` ve `psnames` modüllerini eklemek (program ~60–80 KB büyür).
  `.otag` okuyucusu AROS biçimini ve `.otf` dosyalarını zaten tanıyor.
- Bugünkü pratik çözüm: `.otf` fontları PC'de FontForge gibi bir araçla
  `.ttf`'e çevirip ttf.library ile kurmak.
- Daha büyük bir seçenek: OS 3.2 için güncel FreeType tabanlı bir motor
  (bullet API) yazmak ya da AROS'unkini taşımak (APL). Ayrı bir proje
  kadar iş.

### 3. Gerçek ölçü modunda kerning — orta
- TTF'in harf çifti aralıklarının (ör. "AV", "To", "Ye") uygulanması;
  yazı daha profesyonel görünür.
- `TextLength`/`TextExtent`/`TextFit` de aynı aralıkları hesaba katmalı,
  yoksa ölçüm ve çizim uyuşmaz.
- Sadece `real on` modunda; güvenli modda harfler bitmap fontun
  hücrelerinde kalmak zorunda.

### 4. Hinting seçeneği — kolay
- `hinting normal | light | none` ayarı.
- Bazı fontlarda ve ekranlarda daha yumuşak ya da daha keskin sonuç
  verir; zevke ve monitöre göre seçilir.

### 5. Dock/gösterge yazıları — orta, sonucu belirsiz
- Gizli (ekran dışı) bitmap'e yazan programlar (dock'taki ağ göstergesi
  gibi) şu an `offscreen on` olmadan yumuşatılmıyor.
- Bitmap'in hangi ekrana ait olduğunu daha akıllıca tahmin etmek
  (ör. "friend bitmap" bilgisi, programın penceresinin ekranı).
- Her durumda doğru renk garanti edilemeyebilir.

---

## Altyapı ve kapsam

### 6. Performans — orta
**Önce ölçüm.** DEBUG sürümü `AAText QUIT` sonrasında şu satırı yazar:

```
AAText: timing: N us per Text() call, M us per char, max K us ...
```

WinUAE (68020) ve PiStorm/Emu68 değerleri toplanıp karşılaştırılacak.

**Olası iyileştirmeler** (ölçüm gerektirirse):
- `ReadPixelArray`/`WritePixelArray` yerine `LockBitMapTagList()` ile
  ekran belleğine doğrudan erişim: kopyalamalar ortadan kalkar.
- Kopyalanan alanı küçültmek: sadece harflerin gerçekten kapladığı
  dikey aralık.
- Bunlar 020 sürümü dahil herkesi hızlandırır.

**Ayrı 68040 sürümü gerekmiyor.** Emu68 komutları JIT ile ARM'a
çeviriyor, 040'a özel komut sıralamasının etkisi yok. AAText FPU'yu
sadece açılışta bir kez kullanıyor; asıl zaman büyük olasılıkla ekran
belleği kopyalamasında geçiyor. İstenirse `CPU=68040` ile derlenip
`timing` satırıyla karşılaştırılabilir.

### 7. 6. aşama: AGA ekranlar — zor
- Paletli ekranlarda (8 bit ve altı) `ObtainBestPen()` ile 2–3 ara ton,
  `ReadPixelArray8`/`WritePixelArray8` ile çizim; yeterli renk yoksa
  orijinal `Text()`'e dönüş.
- RTG kartı olmayan gerçek Amiga kullanıcılarına ulaşır.
- Renk sayısı az olduğu için sonuç RTG kadar iyi olmaz; planar ekranda
  okuma/yazma yavaştır.
- Önerim: ancak talep gelirse.

### 8. OS 3.1 / 3.9 uyumluluğu — belirsiz
- Kodun büyük kısmı uyumlu olabilir (graphics.library V39 yeterli), ama
  `.otag` kullanan TrueType kurulumları ve P96 sürümleri farklı olabilir.
- Test gerektirir; öncelik düşük (CLAUDE.md'de de ikincil).

---

## Yayın ve topluluk (geliştirici dışı işler)

- [ ] Aminet'e yükleme: `build/dist/AAText.lha` + `AAText.readme`
      (`Uploader:` satırına e-posta adresi yazılmalı).
- [ ] Pakete ikonlar (`.info`): araç ikonu ve klasör ikonu. İkon dosyaları
      verilirse `make dist` bunları otomatik ekleyecek şekilde
      ayarlanabilir.
- [ ] Sosyal medya ve GitHub Issues'tan gelen geri bildirimleri toplamak.
- [x] GitHub sürümü: https://github.com/Sdursun/AAText/releases/tag/v0.9
