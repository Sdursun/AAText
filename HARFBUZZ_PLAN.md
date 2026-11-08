# harfbuzz.library — yol haritası

Durum: **kararlar alındı (8.11.2026); A6, A0, A1, A2 yapıldı; A3'ün WinUAE kısmı yapıldı, A1200 ve OS 3.1 testi bekliyor.** Her aşamadan önce onay alınır (CLAUDE.md, 9. bölüm).

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
| 4 | **Hedef sistem: en az AmigaOS 3.1** (exec V40, 68020+; 8.11.2026'da 3.0+'tan 3.1+'ya çevrildi). OS 3.2'ye özgü bir şey kullanılmaz. |

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
  açık. **Doğrulandı:** `HB_MINI` bunu zaten kapatıyor (`src/hb-config.hh`:
  `HB_NO_AAT`, `HB_NO_LEGACY`, `HB_NO_BEYOND_64K`, `HB_NO_CUBIC_GLYF`).
- `hb_font_set_face()` 15.0.0'da kullanımdan kaldırıldı: kütüphaneye konmaz.
- Kapsam dışı: raster, vector, GPU kütüphaneleri; Graphite2, HarfRust,
  WebAssembly ve kbts şekillendiricileri.

## 5. Aşamalar

**A0. Hazırlık — YAPILDI (8.11.2026)**
- `tools/fetch-harfbuzz.sh`: 15.1.0, SHA256 GitHub'ın sürüm dosyası için
  verdiği özet (`0fc7c565…0c8d`); `third_party/harfbuzz-15.1.0`.
- Lisans: "Old MIT" (`COPYING`); bazı alt klasörlerin kendi `COPYING`
  dosyaları var, A4'te pakete eklenecek.
- Kaynak bulguları:
  - Tek dosyalı derleme birimi var: `src/harfbuzz.cc` (CoreText,
    DirectWrite, GDI, glib, graphite2, `hb-ft` dosyaları kendi
    `HAVE_*` makroları olmadan boş derlenir).
  - Özel bellek ayırıcı: `hb_malloc_impl`, `hb_calloc_impl`,
    `hb_realloc_impl`, `hb_free_impl` tanımlanınca HarfBuzz bunları
    kullanır (`src/hb.hh`).
  - `HB_NO_MT`: `<atomic>` hiç kullanılmaz, atomik tipler düz
    değişkenlere döner, mutex boştur; yalnızca bir derleyici bariyeri
    (`asm volatile("":::"memory")`) kalır. 6. bölümdeki a) seçeneği
    (tek semafor) böylece doğrulandı: nesnelerin referans sayaçları da
    atomik değil, tüm erişim semafor altında olmalı.
  - `_hb_CrapPool` küresel bir yazılabilir tampon (thread_local değil);
    tek semafor bunu da korur.
- AmigaOS 4 portu (os4coding.net, 27.4.2023): tek sorun meson'un
  otomatik `-pthreads` eklemesi; çözülmeden kalmış, derleme ayrıntısı
  yok. Biz meson kullanmadığımız için bizi etkilemez.

**A1. Derlenebiliyor mu? (statik test programı) — YAPILDI (8.11.2026)**
- `hblib/`: `Makefile`, `src/hbmem.c` (exec havuzu ile `hb_*_impl`),
  `tests/hbinfo.c` (Amiga) ve `tests/hbinfo.py` (PC, uharfbuzz).
- bebbo GCC 16.2 (`m68k-amigaos-g++ -m68020 -Os -std=c++11
  -fno-exceptions -fno-rtti -fno-threadsafe-statics`), `HB_MINI HB_NO_MT
  HB_NO_OPEN HB_NO_MMAP HB_NO_GETENV HB_NO_SETLOCALE HB_NO_ATEXIT
  HB_NO_ERRNO`: hatasız derleniyor (yalnızca `%u`/`long` ve görünürlük
  uyarıları).
- **C++ çalışma zamanından hiçbir sembol gerekmiyor** (`__cxa_*`,
  `operator new`, statik yapıcı yok); yalnızca libgcc kayan nokta
  yardımcıları ve C fonksiyonları (bellek, `memcpy`, `qsort`,
  `snprintf`, matematik). En büyük risk ortadan kalktı.
- Boyut: `harfbuzz.cc` kodu 594 KB; `--gc-sections` ile bağlanan test
  programı 506 KB (C kütüphanesi dahil).
- Doğruluk: WinUAE'de DejaVu Sans (117 satır) ve Source Sans 3 (121
  satır) için kerning, ligatür, Türkçe, birleşen aksan ve Arapça
  testlerinin çıktısı PC'deki HarfBuzz (uharfbuzz, HarfBuzz 15.0.0) ile
  bayt bayt aynı.
- Hız (WinUAE 68060 JIT, bir satır): 39–310 µs. Gerçek 68020/030'da
  ölçülmedi (A1200 o sırada bağlı değildi).
- Yığın: ölçüm `hb_shape` için yaklaşık 1,1–1,4 KB gösterdi; yöntem
  kaba, A3'te 16 KB yığınlı programlarla doğrulanacak.
- `m68k-amigaos-g++ -fno-exceptions -fno-rtti`, `HB_MINI`, `HB_NO_MT`,
  OFF 5 kapalı, boyut için optimize; libstdc++ yok.
- Bellek: exec havuzu (`hb_malloc_impl` vb. ile).
- `hbinfo`: bir TTF ile "AV Wa fi Türkçe" şekillendirip harf numaralarını ve
  ilerlemeleri yazar; PC'deki `hb-shape` ile **bayt bayt aynı** olmalı.
- Ölçülür: boyut, yığın, hız.
- *Kabul:* WinUAE'de PC ile aynı çıktı; ölçümlerle "devam / dur" kararı.

**A2. Kütüphane kabuğu (harfbuzz.library 0.1) — YAPILDI (8.11.2026)**
- `hblib/src/harfbuzz.funcs`: 126 fonksiyon (sıra sabit, yalnızca sona
  eklenir); her biri HarfBuzz 15.1.0 başlıklarıyla otomatik karşılaştırıldı
  (ad, dönüş ve argüman tipleri). Makro olan iki fonksiyon çıkarıldı.
- `hblib/tools/hbfuncs.py`: fonksiyon tablosu, sarmalayıcılar
  (`src/gen/wrappers.c`: argümanlar yapıya, `hb_Call()`), `fd`,
  `inline/harfbuzz.h`, `proto/harfbuzz.h`. Taban: `HarfBuzzBase`.
- `hb_Call()`: tek semafor (iç içe alınabilir); 12 KB'tan az yığın kaldıysa
  32 KB'lık geçici yığın (`StackSwap`).
- `LibInit`: exec V40 (OS 3.1) ve 68020 kontrolü; libnix'in yazılımsal kayan nokta
  ve libm'i için dört `mathieee*.library` açılıyor.
- `src/libcstub.c`: libnix'in stdio'su (ve onu kullanan `snprintf`'i)
  kütüphanede olamaz (`__initstdio` → `exit`): HarfBuzz'ın kullandığı
  biçimler için kendi `snprintf`/`vsnprintf`; `stderr` çıktısı boş.
- Debug derlemesi: libdebug'ın `kprintf`'i bu kütüphanede Amiga'yı
  yeniden başlattı; yerine exec `RawDoFmt` + `RawPutChar` ile kendi
  `kprintf`'imiz.
- Boyut: **harfbuzz.library 508 KB** (kod 474 KB).
- Test (WinUAE, OS 3.2.3): `hbinfo_lib` çıktısı PC'deki HarfBuzz ile
  bayt bayt aynı (DejaVu Sans, Source Sans 3); 4 görev aynı anda (her biri
  30000 şekillendirme): dördü de aynı ve doğru; 4096 ve 6000 baytlık
  yığınla çalışan program doğru sonuç veriyor (geçici yığın yolu); debug
  ölçümü: şekillendirme en çok **1,2 KB** yığın kullanıyor (TTF ve CFF).
- Henüz yok: OS 3.1'de deneme, A1200 (Emu68), geliştirici paketi.
- Başlangıç kodu aatext.library'den (`start.s`, `libinit.c`).
- API alt kümesi (~60–80 fonksiyon): blob, face, font, buffer, `hb_shape`,
  OpenType bilgileri, dil ve yazı sistemi.
- Tek exec semaforu tüm giriş noktalarında (6. bölüm).
- Yığın: aatext.library'deki "az kalmışsa kendi 48 KB yığınında çalış".
- C++ çalışma zamanı: statik yapıcılar, `__cxa_*` sembolleri.
- Başlıklar: proto, inline, clib, fd.
- OS 3.1 uyumu: kullanılan exec fonksiyonları bellek havuzları (V39),
  `StackSwap` (V37), `RawDoFmt`/`RawPutChar` (debug); `mathieee*.library`
  V37. OS 3.1'de çalıştığı henüz **denenmedi** (WinUAE'deki OS 3.1
  yapılandırması gerekiyor).
- *Kabul:* `hbinfo` kütüphaneyle aynı çıktıyı verir; birkaç görev aynı
  anda çalışırken çökme yok; OS 3.1 (WinUAE) ve OS 3.2'de çalışır.

**A3. Hız ve dayanıklılık — WinUAE kısmı YAPILDI (8.11.2026)**
- `tests/hbfuzz.c` + `tests/mkpack.py`: HarfBuzz'ın 532 bozuk/uç durum
  fontu (`test/fuzzing/fonts`) tek paket hâlinde; her font için face, font,
  7 yazı sisteminde şekillendirme, varyasyon, eğik, OpenType bilgileri.
  WinUAE (OS 3.2.3): **532/532, çökme yok**, ~10 sn; iki tur arka arkaya,
  kütüphane bellekten atılınca boş bellek başlangıçtakiyle aynı (sızıntı yok).
- `tests/hbbench.c`: 16 px, WinUAE 68060 JIT: Source Sans 3 Latin
  2385 satır/s (78 karakter), Türkçe 3614, Arapça 6191; DejaVu Sans
  Latin 7564, Türkçe 10605, Arapça 7634.
- Bekleyen: A1200 (Emu68), WinUAE'deki OS 3.1 yapılandırması.
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
