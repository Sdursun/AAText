# AAText – Yol haritası

Bu dosya, 0.12 sürümünden sonra yapılabilecek işleri ve önceliklerini
listeler. Kesin bir takvim değildir; sıra, kullanıcı geri bildirimlerine
göre değişebilir.

**Durum (0.12, yayınlanmadı):** Ana plandaki 1–5. ve 7. aşamalar
tamamlandı. **6. aşama (AGA ekranlar) yapılmadı.**

GitHub'da yayınlanan son sürüm 0.10. 0.11'in kodu GitHub'daki `main`
dalında ama sürümü yayınlanmadı; 0.12 değişiklikleri (ayar programı,
kerning) sadece yerel commit'lerde. Yayın kararı verilmedi.

---

## Tamamlananlar

| Sürüm | İş |
|---|---|
| 0.8 | Otomatik TrueType algılama (`.otag`), gerçek ölçü modu, önbellek, kara liste |
| 0.9 | Yazının dikey konumu bitmap fontla aynı; `real on` boyutu büyük harf yüksekliğine göre |
| 0.10 | Sabit genişlikli fontlar her zaman hücrelerini korur (Shell sorunu) |
| 0.11 | OpenType/CFF (`.otf`) desteği; `ENV:ftcodepage`; `hinting none \| light \| normal \| full` |
| 0.12 | Ayar programı **AATextPrefs**; real modda **kerning**; `ENV:ftcodepage` canlı yeniden okunur; `RELOAD`/`STATUS` |

### AATextPrefs (0.12)
- ReAction, üç sekme: Görünüm, Programlar, Gelişmiş.
- Türkçe/İngilizce (locale.library kataloğu), GlowIcon.
- Değişiklikler çalışan AAText'e anında uygulanır (mesaj portu `AAText`,
  protokol sürüm 2); Kaydet / Kullan / İptal.
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

### Kerning (0.12)
- `kerning on|off`, real modda varsayılan açık; fontun `kern` tablosundan.
- Çizim ve `TextLength`/`TextExtent`/`TextFit` (iki yönde) aynı değerleri
  kullanır; vamos'taki ölçüm testleri ve WinUAE'de `cp_x == TextLength`
  kontrolü ile doğrulandı.
- `real` gibi yeniden başlatma gerektirir; AATextPrefs'te onay kutusu var.
- Sınır: kerning'i sadece `GPOS` tablosunda tutan fontlar (ör. Tahoma)
  kerning almaz (bkz. madde 5).

---

## Sıradaki işler

### 1. A1200 (PiStorm) testleri — kullanıcı
A1200'e en son bağlanıldığında ağdan düştü; dosyalar kopyalanmıştı ama
hiçbir şey çalıştırılmadı.
- [ ] AATextPrefs ile **Kaydet**: ENVARC:'ın yazılabildiği gerçek bir
      sistemde.
- [ ] freetype2.library ile kurulmuş bir fontta "Türkçe ENV:ftcodepage
      yaz" düğmesinin etkisi.
- [ ] `real on` ile kerning'in gerçek programlarda (Workbench, MUI,
      ReAction) düzen bozmadan çalıştığı.
- [ ] `textbench` ile performans ölçümü (madde 3).

### 2. Yayın — kolay, karar bekliyor
- [ ] GitHub sürümü (0.12; 0.11 ayrıca yayınlanmadı).
- [ ] Aminet: `build/dist/AAText.lha` + `AAText.readme` — `Uploader:`
      satırındaki `<EMAIL>` doldurulmalı.
- [ ] Sosyal medya ve GitHub Issues'tan gelen geri bildirimleri toplamak.

### 3. Performans — ölçüm sürüyor
`tests/textbench.c` bir satırı 2000 kez çizip süreyi ölçer.

İlk sonuçlar, WinUAE (68040 JIT, UAEGFX, 1024×768×32):

| Font | AAText kapalı | AAText açık |
|---|---|---|
| Arial 13 | 32 µs/satır | 40 µs/satır |
| Arial 19 | 38 µs/satır | 63 µs/satır |
| topaz 8 (bitmap) | 7 µs/satır | 8 µs/satır |

WinUAE'de işlemci ve ekran kartı PC hızında çalıştığı için bu sonuç
gerçek donanım için belirleyici değil. Eksik ölçümler:
- [ ] PiStorm/Emu68 (A1200).
- [ ] Gerçek 68020 hızı: test WinUAE'si JIT kapalı, 68020 ayarıyla
      (kullanıcı izni bekleniyor).

Olası iyileştirmeler (ölçüm gerektirirse):
- `ReadPixelArray`/`WritePixelArray` yerine `LockBitMapTagList()` ile
  ekran belleğine doğrudan erişim: kopyalamalar ortadan kalkar.
- Kopyalanan alanı küçültmek: sadece harflerin gerçekten kapladığı
  dikey aralık.

**Ayrı 68040 sürümü gerekmiyor.** Emu68 komutları JIT ile ARM'a çeviriyor;
AAText FPU'yu sadece açılışta kullanıyor.

### 4. Test altyapısı — **yapıldı**
- `capsize` testi kendi ayar dosyasıyla (`tests/capsize.prefs`, piksel
  boyutu yok) çalışıyor ve geçiyor.
- `.\test.ps1 -All` (`tests/run-all.sh`) bütün test modlarını gereken ayar
  dosyalarıyla çalıştırıp dönüş kodlarını kontrol ediyor; 8 test.

### 5. GPOS kerning — orta, talep gelirse
- `kern` tablosu olmayan fontlar (ör. Tahoma) için `GPOS` PairPos
  (format 1/2) çift ayarlamalarını elle okumak. HarfBuzz gibi tam bir
  shaping motoru gerekmez, ama `GPOS`'un lookup/coverage/class yapısını
  okumak gerekir.

### 6. Dock/gösterge yazıları — orta, sonucu belirsiz
- Gizli (ekran dışı) bitmap'e yazan programlar `offscreen on` olmadan
  yumuşatılmıyor.
- Bitmap'in hangi ekrana ait olduğunu daha akıllıca tahmin etmek
  (ör. "friend bitmap" bilgisi, programın penceresinin ekranı).
- Her durumda doğru renk garanti edilemeyebilir.

### 7. AGA ekranlar (6. aşama) — zor, talep gelirse
- Paletli ekranlarda (8 bit ve altı) `ObtainBestPen()` ile 2–3 ara ton,
  `ReadPixelArray8`/`WritePixelArray8` ile çizim; yeterli renk yoksa
  orijinal `Text()`'e dönüş.
- RTG kartı olmayan gerçek Amiga kullanıcılarına ulaşır.
- Renk sayısı az olduğu için sonuç RTG kadar iyi olmaz; planar ekranda
  okuma/yazma yavaştır.

### 8. OS 3.1 / 3.9 uyumluluğu — belirsiz, düşük öncelik
- AAText'in büyük kısmı uyumlu olabilir (graphics.library V39 yeterli),
  ama `.otag` kullanan TrueType kurulumları ve P96 sürümleri farklı olabilir.
- AATextPrefs OS 3.2'ye özgü: `WINDOW_NewMenu` (window.class V47) kullanır.
- Test gerektirir (CLAUDE.md'de de ikincil).

---

## Önerilen sıra

1. A1200 testleri (madde 1) ve performans ölçümü (madde 3).
2. Yayın (madde 2).
3. Geri bildirim toplamak; gerçek hatalar her zaman yeni özelliklerden
   önce gelir.
4. Her değişiklikten sonra `.\test.ps1 -All`.
5. Diğerleri talebe göre. AGA ancak istek gelirse.
