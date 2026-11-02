# aatext.library 1.1

AAText'in kütüphanesi: FreeType 2.14.3'ü AmigaOS 3 (68020+) için paylaşımlı
kütüphane olarak sunar ve diskfont.library için bir bullet (outline) motoru
içerir (`OT_Engine "aatext"`). Sistemdeki **freetype2.library**'ye (1.3, 2002,
FreeType 2.1, Aminet `util/libs/freetype2_lib`) dokunmaz; ikisi yan yana durur.
Fonksiyon sırası 1.3 ile aynıdır, `.otag` etiketleri de FTManager'ınkilerle aynı.

> Kütüphane 9.10.2026'da `freetype2.library 2.14`'ten `aatext.library 1.0`'a
> yeniden adlandırıldı. 2.11.2026'dan beri AAText deposunun `aatextlib/`
> klasöründe (önceden ayrı bir `freetype2` klasörüydü).

## Klasörler

| Klasör | İçerik |
|---|---|
| `src/` | kütüphanenin kaynağı |
| `src/aatext.funcs` | **LVO sırasının tek kaynağı** |
| `src/gen/` | `functable.inc` (üretilir, elle değiştirme) |
| `include/` | istemci başlıkları: `proto`, `inline`, `clib`, `fd` (üretilir), `libraries/aatext.h` |
| `tests/` | `ft2info` (FreeType API), `ft2bullet` (bullet API, diskfont gibi), `ft2dfont` (diskfont üzerinden pencereye çizim) |
| `tools/genfuncs.py` | `aatext.funcs` → fonksiyon tablosu ve başlıklar |
| `docs/` | Aminet readme, kullanıcı belgeleri (EN, TR) |
| `build/<cpu>/` | derlenmiş kütüphane ve testler; `build/lvo.txt` LVO haritası |
| `dist/` | Aminet paketi (`aatext_lib.lha`, `aatext_lib.readme`) |
| `../third_party/freetype-2.14.3/` | değiştirilmemiş FreeType kaynağı (AAText ile ortak, `tools/fetch-freetype.sh`) |
| `ttengine-7.2/` | referans: ttengine.library 7.2 kaynağı (derlemede kullanılmaz) |
| `reference/old-freetype2.library/` | eski freetype2.library 1.3 ikilisi ve örnek `.otag`/`.font` dosyaları (karşılaştırma için) |

## Kaynak dosyalar

| Dosya | Görev |
|---|---|
| `start.s` | dosyanın ilk kodu (çalıştırılırsa -1 döner), `ft2_CallSwapped` (StackSwap) |
| `libinit.c` | RomTag, init/open/close/expunge, fonksiyon tablosu, büyük yığın |
| `bullet.c` | OpenEngine/CloseEngine/SetInfoA/ObtainInfoA/ReleaseInfoA |
| `ftsystem.c` | FreeType sistem katmanı: exec havuzu, AmigaDOS akışı, `getenv` |
| `ft2_option.h`, `ft2_module.h` | FreeType yapılandırması ve sürücü listesi |
| `ftapi.h` | fonksiyon tablosundaki her FreeType fonksiyonunun başlığı |
| `lib.h` | kütüphane adı, sürümü (`LIB_NAME`, `LIB_VERSION`) |

## Derleme

Docker ile (`ghcr.io/rondoval/amiga-build-container`, bebbo gcc + NDK 3.2):

```
.\build.ps1              # build/68020/aatext.library
.\build.ps1 CPU=68060    # build/68060/aatext.library
.\build.ps1 DEBUG=1      # build/68020-debug, kprintf seri porta
.\build.ps1 test         # build/68020/tests/ft2info, ft2bullet, ft2dfont
.\build.ps1 headers      # aatext.funcs değişince başlıkları üret
.\build.ps1 dist         # dist/aatext_lib.lha
```

## Mimari kararlar

- **Ad.** `aatext.library`, motor adı `aatext`, taban `AATextBase`. Sistemin
  `freetype2.library`'sinin üzerine yazılmaz; FTManager fontları (`OT_Engine
  "freetype2"`) eski kütüphaneyle çalışmaya devam eder. Yeni motoru kullanmak
  için `.otag`'ın motoru `aatext` olmalı (AAText yol haritası, madde 9: font
  kurucu).
- **ABI.** Bullet fonksiyonları argümanlarını register'da alır (A0/A1). FreeType
  fonksiyonları (LVO -90'dan itibaren) argümanlarını **yığında, C sırasıyla**
  alır; tabloda doğrudan FreeType'ın fonksiyonu durur. Bu, freetype2.library
  1.3 ikilisinin disassembly'sinden alındı (`linkw a5` / `a5@(8)` = ilk argüman).
- **LVO sırası.** -30…-720 arası freetype2.library 1.3 ile aynı (ikilinin
  fonksiyon tablosuyla karşılaştırıldı; AROS `freetype2.conf`'un 2019 hali).
  freetype2.library için yazılmış bir program sadece kütüphane adını
  değiştirerek çalışır. Yeni fonksiyonlar sadece **sona** eklenir.
- **Bellek.** Her `FT_Library` için ayrı exec havuzu (`FT_New_Memory`). Havuzlar
  task güvenli değil; FreeType'ın "bir FT_Library aynı anda tek görevde" kuralı
  bunu kapsar. Her bullet motoru kendi `FT_Library`'sini ve semaforunu taşır.
- **Yığın.** Bullet çağrıları, çağıranın yığınında 32 KB'tan az kalmışsa 48 KB'lık
  geçici bir yığına geçer (`ft2_BigStack`).
- **C kütüphanesi.** Başlangıç kodu yok (`-nostartfiles`); libnix'ten sadece
  string fonksiyonları, `qsort`, `strtol`, `setjmp/longjmp` gelir (link
  haritası: `build/68020/aatext.map`). `malloc` ve stdio yok.
- **Bullet davranışı.** Boyut hesabı, GlyphMap alanları, genişlik listesi ve
  kerning freetype2.library 1.3 ile aynı formüller (font boyları değişmez).
  1 bit gliflerde `FT_LOAD_TARGET_MONO` hinting'i kasıtlı bir farktır (daha
  düzgün glifler).

## Test (WinUAE)

```
MakeDir RAM:aat
Copy Desktop:freetype2/build/68020/aatext.library RAM:aat/
Copy Desktop:freetype2/build/68020/tests/#? RAM:aat/
RAM:aat/ft2info LIB RAM:aat/aatext.library FONT Fonts:_ttf/verdana.ttf
RAM:aat/ft2bullet FONTS:verdanaregular.otag SIZE 13 ART WIDTHS LIB RAM:aat/aatext.library
```

- `ft2bullet`, `.otag` hangi motoru yazarsa yazsın verilen kütüphaneyi kullanır;
  1.3 ile karşılaştırmak için `LIB freetype2.library`.
- diskfont yolu: `.otag`'ın motorunu `aatext` yapıp `.font` ile birlikte
  `RAM:aat/fonts/` içine koymak, `Assign LIBS: RAM:aat ADD`, sonra
  `RAM:aat/ft2dfont FONT RAM:aat/fonts/<ad>.font`; bitince
  `Assign LIBS: RAM:aat REMOVE`. Kullanıcının `FONTS:` ve `LIBS:`'ine yazılmaz.
- AAText çalışırken ekrandaki yazı AAText'in çizimidir; `ft2dfont` her satırı bir
  de fontun kendi bitmap'inden çizer (motorun gerçek çıktısı).

## Lisans

FreeType: FreeType License (`third_party/freetype-2.14.3/docs/FTL.TXT`). Kütüphane kodu: MIT
(`LICENSE`).
