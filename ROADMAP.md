# AAText – Yol haritası

Bu dosya, 0.12 sürümünden sonra yapılabilecek işleri ve önceliklerini
listeler. Kesin bir takvim değildir; sıra, kullanıcı geri bildirimlerine
göre değişebilir.

**Durum (0.12, yayınlanmadı):** Ana plandaki 1–5. ve 7. aşamalar
tamamlandı. **6. aşama (AGA ekranlar) yapılmadı.**

GitHub'da yayınlanan son sürüm 0.10. 0.11'in kodu GitHub'daki `main`
dalında ama sürümü yayınlanmadı; 0.12 değişiklikleri (ayar programı)
sadece yerel commit'lerde. Yayın kararı verilmedi.

---

## Tamamlananlar

| Sürüm | İş |
|---|---|
| 0.8 | Otomatik TrueType algılama (`.otag`), gerçek ölçü modu, önbellek, kara liste |
| 0.9 | Yazının dikey konumu bitmap fontla aynı; `real on` boyutu büyük harf yüksekliğine göre |
| 0.10 | Sabit genişlikli fontlar her zaman hücrelerini korur (Shell sorunu) |
| 0.11 | OpenType/CFF (`.otf`) desteği; `ENV:ftcodepage`; `hinting none \| light \| normal \| full` |
| 0.12 | Ayar programı **AATextPrefs** (aşağıda); real modda kerning; `ENV:ftcodepage` canlı yeniden okunur; `RELOAD`/`STATUS` |

### AATextPrefs (0.12)
- ReAction, üç sekme: Görünüm, Programlar, Gelişmiş.
- Türkçe/İngilizce (locale.library kataloğu), GlowIcon.
- Değişiklikler çalışan AAText'e anında uygulanır (mesaj portu `AAText`);
  Kaydet / Kullan / İptal.
- Prefs menüleri: Aç, Farklı Kaydet, Varsayılan Ayarlar, Son Kaydedilen,
  Geri Al.
- Kara liste: elle yazma, exe dosyası seçme, çalışan programlardan seçme.
- Gelişmiş: otomatik algılama, offscreen, önbellek boyutu ve kullanımı,
  karakter seti, "Türkçe ENV:ftcodepage yaz" düğmesi.
- `PUBSCREEN=`, `LANGUAGE=`; 640×256 topaz 8 ekrana sığar.
- Bilinçli olarak yapılmayanlar:
  - Fontlar sekmesi: fontlar zaten otomatik bulunur; elle eşleme sadece
    dosyada.
  - İkonun ToolTypes'ını okuma: gerek görülmedi.

---

## Sıradaki işler

### 1. Yayın — kolay, karar bekliyor
- [ ] A1200'de son test: AATextPrefs ile **Kaydet** (ENVARC:'ın yazılabildiği
      gerçek bir sistemde), freetype2.library ile kurulmuş bir fontta
      "Türkçe ENV:ftcodepage yaz" düğmesinin etkisi.
- [ ] GitHub sürümü (0.12; 0.11 ayrıca yayınlanmadı).
- [ ] Aminet: `build/dist/AAText.lha` + `AAText.readme` — `Uploader:`
      satırındaki `<EMAIL>` doldurulmalı.
- [ ] Sosyal medya ve GitHub Issues'tan gelen geri bildirimleri toplamak.

### 2. Performans — orta, önce ölçüm
DEBUG sürümü `AAText QUIT` sonrasında şu satırı yazar:

```
AAText: timing: N us per Text() call, M us per char, max K us ...
```

WinUAE (68020) ve PiStorm/Emu68 değerleri toplanıp karşılaştırılacak.

Olası iyileştirmeler (ölçüm gerektirirse):
- `ReadPixelArray`/`WritePixelArray` yerine `LockBitMapTagList()` ile
  ekran belleğine doğrudan erişim: kopyalamalar ortadan kalkar.
- Kopyalanan alanı küçültmek: sadece harflerin gerçekten kapladığı
  dikey aralık.

**Ayrı 68040 sürümü gerekmiyor.** Emu68 komutları JIT ile ARM'a çeviriyor;
AAText FPU'yu sadece açılışta kullanıyor; asıl zaman büyük olasılıkla ekran
belleği kopyalamasında geçiyor.

### 3. Gerçek ölçü modunda kerning — **yapıldı (0.12)**
- `kerning on|off` (real modda varsayılan açık), fontun `kern` tablosundan;
  çizim ve `TextLength`/`TextExtent`/`TextFit` aynı değerleri kullanır.
- Kalan sınır: kerning bilgisini sadece `GPOS` tablosunda tutan fontlar
  (ör. Tahoma) kerning almaz; bunun için bir shaping motoru (HarfBuzz)
  gerekirdi. Talep gelirse `GPOS` çift ayarlamaları (PairPos format 1/2)
  elle okunabilir — orta iş.

### 4. Dock/gösterge yazıları — orta, sonucu belirsiz
- Gizli (ekran dışı) bitmap'e yazan programlar `offscreen on` olmadan
  yumuşatılmıyor.
- Bitmap'in hangi ekrana ait olduğunu daha akıllıca tahmin etmek
  (ör. "friend bitmap" bilgisi, programın penceresinin ekranı).
- Her durumda doğru renk garanti edilemeyebilir.

### 5. AGA ekranlar (6. aşama) — zor, talep gelirse
- Paletli ekranlarda (8 bit ve altı) `ObtainBestPen()` ile 2–3 ara ton,
  `ReadPixelArray8`/`WritePixelArray8` ile çizim; yeterli renk yoksa
  orijinal `Text()`'e dönüş.
- RTG kartı olmayan gerçek Amiga kullanıcılarına ulaşır.
- Renk sayısı az olduğu için sonuç RTG kadar iyi olmaz; planar ekranda
  okuma/yazma yavaştır.

### 6. OS 3.1 / 3.9 uyumluluğu — belirsiz, düşük öncelik
- AAText'in büyük kısmı uyumlu olabilir (graphics.library V39 yeterli),
  ama `.otag` kullanan TrueType kurulumları ve P96 sürümleri farklı olabilir.
- AATextPrefs OS 3.2'ye özgü: `WINDOW_NewMenu` (window.class V47) kullanır.
- Test gerektirir (CLAUDE.md'de de ikincil).

---

## Önerilen sıra

1. A1200 testi ve yayın (madde 1).
2. Geri bildirim toplamak; gerçek hatalar her zaman yeni özelliklerden
   önce gelir.
3. Performans ölçümü (madde 2); sonuca göre iyileştirme.
4. Diğerleri talebe göre. AGA ancak istek gelirse.
