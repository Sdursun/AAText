# harfbuzz.library — yol haritası

Durum: **kararlar alındı (8.11.2026); A6 yapıldı, A0 sırada.** Her aşamadan önce onay alınır (CLAUDE.md, 9. bölüm).

---

## 1. Amaç ve kapsam

- **Kimin için:** kendi Unicode metnini yöneten AmigaOS 3 programları
  (tarayıcı, kelime işlemci, masaüstü yayıncılık): kerning, ligatür, aksan
  yerleştirme ve karmaşık yazılar için doğru harf konumları.
- **Kimin için değil:** AAText'in `Text()` yaması. Bayt başına bir karakter
  alan ve karakter başına genişlik bekleyen sistem yazısı şekillendirmeden
  çok az yararlanır; ligatürler `TextLength()` ile çalışan editörleri bozar.
  AAText'in GPOS kerning ihtiyacı ayrı ve küçük bir iştir (8. bölüm, A6).
- **Lisans:** HarfBuzz "Old MIT"; AAText ve FreeType lisanslarıyla uyumlu.

## 2. Alınan kararlar (8.11.2026)

| # | Karar |
|---|---|
| 1 | **Ayrı bir kütüphane:** `harfbuzz.library`; aatext.library'nin içine girmez. |
| 2 | **FreeType'a bağımlı değil:** fontları HarfBuzz'ın kendi OpenType okuyucusu bellekten okur (`hb_ot_font_set_funcs`); `hb_ft` köprüsü yok. |
| 3 | **`HB_MINI`:** yalnızca OpenType şekillendirmesi; AAT ve eski font biçimleri çıkar. |
| 4 | **Hedef sistem: AmigaOS 3.0+** (68020+). Yalnızca V39 exec/dos fonksiyonları; OS 3.2'ye özgü bir şey kullanılmaz. |

## 3. Sonraki kararlar (8.11.2026, kullanıcı: "önerinle başla, sen karar ver")

1. **Çağırma biçimi:** aatext.library'deki FreeType fonksiyonları gibi
   argümanlar yığından, C sırasıyla; tablo ve başlıklar `genfuncs.py` ile.
2. **Sıra:** önce A6 (AAText'te GPOS kerning), sonra A0–A1.
3. **Yön:** HarfBuzz'ın getirdiği her şey ileride sistem yazısının
   görünümünü iyileştirmek için kullanılacak; ClearType (subpixel) bunun
   parçası. Ayrı iş (A7): kurulum programı ClearType'ı açıklamasıyla
   sorsun (ölçeklenmiş ekranlarda renkli kenar yüzünden varsayılan kapalı
   kalıyor), subpixel çizim FreeType 2.15'in v40 ClearType
   iyileştirmeleriyle birlikte gözden geçirilsin.

## 4. Kaynak sürümü

- **HarfBuzz 15.1.0** (4.11.2026, github.com/harfbuzz/harfbuzz). FreeType'taki
  gibi sürümü ve SHA256'sı sabitlenmiş bir betikle indirilir
  (`tools/fetch-harfbuzz.sh`); `third_party/` git'e girmez.
- C++11 (`cpp_std=c++11`): Docker'daki `m68k-amigaos-g++` ile derlenmeli
  (doğrulanmadı).
- 15.0.0'dan beri **atomik işlemler derleme sırasında değiştirilemiyor**
  (bkz. 6. bölüm).
- 15.0.0'dan beri **Open Font Format 5. baskı** desteği (65536'dan fazla
  harf, `GLYF` içinde kübik eğriler, 24 bitlik GSUB/GPOS) varsayılan olarak
  açık. Amiga programları için gereksiz; kapatan derleme seçeneği aranacak
  (`HB_NO_BEYOND_64K` benzeri bir ad bekleniyor, **doğrulanmadı**).
- `hb_font_set_face()` 15.0.0'da kullanımdan kaldırıldı: kütüphaneye konmaz.
- Kapsam dışı: raster, vector, GPU kütüphaneleri; Graphite2, HarfRust,
  WebAssembly ve kbts şekillendiricileri.

## 5. Aşamalar

**A0. Hazırlık**
- `tools/fetch-harfbuzz.sh` (15.1.0, SHA256).
- AmigaOS 4 portunun derleme notları (os4coding.net: meson'un otomatik
  `-pthreads`'i sorun çıkarmış; biz kendi Makefile'ımızı kullanacağız).
- *Kabul:* kaynak indiriliyor, sürüm ve lisans belgelendi.

**A1. Derlenebiliyor mu? (statik test programı)**
- `m68k-amigaos-g++ -fno-exceptions -fno-rtti`, `HB_MINI`, `HB_NO_MT`,
  OFF 5 kapalı, boyut için optimize; libstdc++ yok.
- Bellek: exec havuzu (`hb_malloc_impl` vb. ile).
- `hbinfo`: bir TTF ile "AV Wa fi Türkçe" şekillendirip harf numaralarını ve
  ilerlemeleri yazar; PC'deki `hb-shape` ile **bayt bayt aynı** olmalı.
- Ölçülür: boyut, yığın, hız.
- *Kabul:* WinUAE'de PC ile aynı çıktı; ölçümlerle "devam / dur" kararı.

**A2. Kütüphane kabuğu (harfbuzz.library 0.1)**
- Başlangıç kodu aatext.library'den (`start.s`, `libinit.c`).
- API alt kümesi (~60–80 fonksiyon): blob, face, font, buffer, `hb_shape`,
  OpenType bilgileri, dil ve yazı sistemi.
- Tek exec semaforu tüm giriş noktalarında (6. bölüm).
- Yığın: aatext.library'deki "az kalmışsa kendi 48 KB yığınında çalış".
- C++ çalışma zamanı: statik yapıcılar, `__cxa_*` sembolleri.
- Başlıklar: proto, inline, clib, fd.
- OS 3.0 uyumu: exec bellek havuzları (`CreatePool`/`AllocPooled`) V39'da
  var; daha yeni bir fonksiyon kullanılmaz. aatext.library'nin başlangıç
  kodunun OS 3.0'da çalıştığı **doğrulanmadı** (AAText OS 3.2 hedefli).
- *Kabul:* `hbinfo` kütüphaneyle aynı çıktıyı verir; birkaç görev aynı
  anda çalışırken çökme yok; OS 3.1 (WinUAE) ve OS 3.2'de çalışır.

**A3. Hız ve dayanıklılık**
- `hbbench` (aabench gibi): saniyede şekillendirilen satır ve karakter;
  WinUAE ve A1200 (Emu68).
- HarfBuzz'ın bozuk font test dosyaları; 16 KB yığınlı programlar.

**A4. Belgeler ve dağıtım**
- Türkçe ve İngilizce belgeler, örnekler, Aminet `.readme`, `Developer/`.
- *Kabul:* örnek program yalnızca paketteki dosyalarla derlenir.

**A5. Bakım**
- HarfBuzz son iki ayda üç sürüm çıkardı; bozuk fontlarla ilgili
  düzeltmeleri olan sürümler izlenir.

**A6. AAText için GPOS kerning (ayrı, HarfBuzz'a bağlı değil) — YAPILDI**
- Ölçüm (8.11.2026): 17 popüler Google fontunun 13'ünde kerning yalnızca
  GPOS'ta (Lato, Montserrat, Noto Sans, Inter, Fira Sans, Source Sans 3,
  ...); AAText bunlarda hiç kerning uygulamıyordu.
- AAText'in kendi GPOS okuyucusu (`src/glyphs.c`, `KernGPOS`): `latn`/`DFLT`
  `kern` lookup'ları, PairPos format 1 ve 2, Extension; GPOS varsa
  `kern` tablosu kullanılmaz (HarfBuzz gibi). Kerning tablosu artık
  değişken boyutlu (en çok 32768 çift).
- Doğrulama: Python referansı uharfbuzz ile 9 fontta tüm çiftlerde aynı
  (yumuşak tire hariç); Amiga'daki C kodu referansla aynı çift sayısını
  veriyor (Source Sans 3 18 px: 617, Titillium Web 14 px: 441).

**A7. ClearType (subpixel) iyileştirmesi**
- Kurulumda ClearType sorusu; subpixel yolunun v40 hinting, LCD filtresi ve
  gamma ile gözden geçirilmesi; 1:1 ve ölçeklenmiş ekranda A1200 testi.

## 6. Görevler arası güvenlik

HarfBuzz'ın atomik işlemleri 15.0.0'dan beri dışarıdan değiştirilemiyor.

- **a) Önerilen:** `HB_NO_MT` + tüm giriş noktalarında tek bir exec semaforu.
  Basit ve güvenli; aynı anda yalnızca bir görev şekillendirir.
- **b)** GCC'nin atomik işlemleri (68020+'da `CAS`). Amiga'da
  okuma-değiştirme-yazma döngüleri chip RAM'de güvenilir değil; Emu68'in
  davranışı bilinmiyor. **Doğrulanmadı**, denemeden seçilmez.

## 7. Riskler

| Risk | Etkisi | Önlem |
|---|---|---|
| AmigaOS 3 paylaşılan kütüphanesinde C++ (statik yapıcılar, çalışma zamanı sembolleri) | Kütüphane kurulamayabilir | A1'de önce statik program |
| Boyut | Disk ve bellek | `HB_MINI`, OFF 5 kapalı, boyut için optimize, kullanılmayan kodu bağlamada atmak |
| 68020/030'da hız | Programlarda yavaşlık | A3'te ölçmek; programlar sonuçları önbelleğe alır |
| Yığın taşması | Çökme | aatext.library'deki büyük yığın yöntemi |
| Atomik işlemler | Görevler arası bozulma | 6. bölüm, a) |
| Sık sürüm | Bakım yükü | Sürüm + SHA256 sabitleme; düzeltme sürümlerini izlemek |

## 8. Kaynaklar

- https://github.com/harfbuzz/harfbuzz (15.1.0, 15.0.0 ve 14.6.0 sürüm notları)
- https://github.com/harfbuzz/harfbuzz/blob/main/CONFIG.md (`HB_MINI`,
  `HB_LEAN`, `HB_TINY`, `HB_NO_MT`, `HB_CONFIG_OVERRIDE_H`)
- https://os4coding.net/node/2547 (AmigaOS 4 portu)
