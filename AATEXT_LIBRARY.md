# aatext.library 1.0 – AAText'in kütüphanesi

AAText için ayrı bir paylaşımlı kütüphane geliştirildi: **aatext.library 1.0**
(FreeType 2.14.3). Kaynağı `C:\Users\Serkan\Desktop\freetype2` klasöründe
(`src/`, derleme `build/`, Aminet paketi `dist/`; klasör adı tarihsel olarak
`freetype2` kaldı). Bu dosya, kütüphanenin AAText geliştirmesinde nasıl
kullanılacağını ve AAText'i ilgilendiren özelliklerini listeler.

Kütüphanenin iki işi var:

1. **FreeType API** – AAText'in gömülü FreeType'ı yerine.
2. **diskfont motoru** – `.otag` dosyasında `OT_Engine "aatext"` yazan fontlar
   için (bullet.library API'si).

Sistemdeki **freetype2.library 1.3**'e (FTManager ile gelen, FreeType 2.1)
**dokunmaz**: ikisi yan yana durur, FTManager fontları (`OT_Engine
"freetype2"`) eskisiyle çalışmaya devam eder. (Kütüphane önce
`freetype2.library 2.14` olarak yazıldı, 9.10.2026'da bu karar için
`aatext.library` yapıldı.)

---

## 1. FreeType'ı gömmek yerine kütüphaneyi kullanmak

AAText bugün FreeType'ı statik gömüyor (`third_party/freetype-2.14.3`,
`src/ft/`). `build/68020/AAText` 350 KB; bunun büyük kısmı FreeType.
Kütüphaneyle:

- AAText yaklaşık 50–60 KB'a iner; `aatext.library` AAText paketinde
  `Libs/` altında gelir.
- `aa_ftoption.h` / `aa_ftmodule.h` / `aa_ftsystem.c` gerekmez; kütüphane daha
  çok sürücü içerir (Type 1, CID, PCF, BDF, FNT, WOFF, variable font).
- FreeType güncellemesi ve AAText güncellemesi ayrı yapılabilir.

AAText'in kullandığı her fonksiyon kütüphanede var:

| AAText çağrısı | LVO | Not |
|---|---|---|
| `FT_New_Library`, `FT_Done_Library` | -648, -654 | AAText kendi `FT_MemoryRec`'ini verebilir |
| `FT_Add_Default_Modules` | -666 | kütüphanedeki tüm sürücüleri ekler |
| `FT_New_Memory_Face` | -120 | |
| `FT_New_Size`, `FT_Activate_Size`, `FT_Done_Size` | -270, -282, -276 | |
| `FT_Set_Pixel_Sizes` | -150 | |
| `FT_Load_Glyph`, `FT_Render_Glyph` | -162, -186 | |
| `FT_Get_Char_Index` | -168 | |
| `FT_Get_Advance` | -912 | |
| `FT_Load_Sfnt_Table` | -708 | |
| `FT_Property_Set` | -810 | |
| `FT_MulFix` | -396 | |

`FT_New_Memory` / `FT_Done_Memory` FreeType'ın iç (FT_BASE) fonksiyonları,
kütüphanede **yok**. İki yol var:

- `FT_Init_FreeType(&lib)`: kütüphane bu FT_Library için kendi exec havuzunu
  açar, `FT_Done_FreeType` hepsini kapatır.
- Ya da bugünkü `aa_ftsystem.c` mantığını AAText içinde tutup doldurulan
  `FT_MemoryRec`'i `FT_New_Library(memory, &lib)` ile vermek. Geri çağrılar
  (alloc/free/realloc) kütüphaneden AAText'in koduna normal C çağrısı olarak
  gelir; bu çalışır.

## 2. Çağırma şekli

```c
#include <proto/aatext.h>       /* FreeType başlıkları + inline makrolar */
struct Library *AATextBase;

AATextBase = OpenLibrary("aatext.library", 1);
```

- FreeType fonksiyonları argümanlarını **yığında** alır (C sırası), A6
  kullanılmaz. `<inline/aatext.h>` her çağrıyı kütüphane vektörüne yapılan
  bir C çağrısına çevirir; AAText'in kaynak kodunda FreeType çağrıları
  değişmez.
- `inline/aatext.h` FreeType başlıklarından **sonra** gelmeli
  (`proto/aatext.h` bunu yapar). AAText'in kendi `#include <ft2build.h>`
  satırları proto'dan önce kalabilir.
- Başlıklar: `freetype2/include` (proto, inline, clib, fd, libraries) +
  `freetype2/freetype-2.14.3/include`. Makefile'da
  `-I../freetype2/include -I../freetype2/freetype-2.14.3/include`.
- Kütüphane yoksa AAText başlamamalı ve bunu söylemeli (ya da statik
  FreeType'lı bir yapı ayrıca dağıtılır – karar verilmedi).

## 3. Görev (task) ve yığın kuralları

- FreeType kuralı: bir `FT_Library` ve ondan açılan her şey aynı anda tek
  bir göreve aittir. AAText tek bir `aa_FTLib`'i `aa_GlyphSem` altında farklı
  görevlerden kullanıyor; semafor sıralı erişim sağladığı için bu doğru kalır.
- Her `FT_Library`'nin belleği kendi exec havuzundan gelir; havuzlar task
  güvenli değil, AAText'in semaforu bunu da kapsar.
- Kütüphane FreeType API çağrılarında **yığın değiştirmez** (sadece motor
  çağrılarında değiştirir). AAText'in `RunOnRenderStack`'i aynen gerekli.
- Font dosyasını kütüphaneye `FT_New_Face` ile okutmak (AmigaDOS, 1 KB
  tampon) çağıranın bir **process** olmasını ister. Text() yamasında
  input.device gibi task'lar da olabileceğinden AAText dosyayı kendisi
  belleğe okuyup `FT_New_Memory_Face` kullanmaya devam etmeli.

---

## 4. diskfont motoru (`OT_Engine "aatext"`)

`.otag` etiketleri FTManager / freetype2.library ile aynı; bir `.otag`'ın
motoru `"freetype2"`den `"aatext"`e çevrilince aatext.library ile çalışır
(WinUAE'de denendi). Font boyları 1.3 ile aynı formülle hesaplanır.
freetype2.library 1.3'e göre farklar:

- **1 bit glifler siyah-beyaz için hint'leniyor** (`FT_LOAD_TARGET_MONO`):
  1.3 gri hinting'i eşikliyordu, küçük boyutlarda W, a, e kırılıyordu. Bazı
  karakterler bir piksel geniş/dar olur (Verdana 13'te 'a' 4,6 → 5 piksel).
  AAText'in safe metrics modu genişlikleri diskfont fontundan aldığı için
  bunu otomatik izler.
- **8 bit antialiased glif haritaları**: `ObtainInfoA(OT_GlyphMap8Bit)`
  (`OT_Level0 | OT_Indirect | 0x1a`, NDK 3.2) ve eski `0x80001108`. AGA
  aşaması (madde 7) için kullanılabilir.
- `OT_GlyphCode_32` / `OT_WidthList32`, `OT_EmboldenX/Y`, `OT_SetFactor`,
  `OT_NumGlyphs`.
- `OT_Spec9_Hinter`: 0 font, 1 autohinter, 2 yok, **3 light** (sadece aatext).
- Symbol kodlamalı fontlar (Symbol.ttf): karakter `U+F0xx`'te aranır.
- PCF/BDF/FNT bitmap fontlar açılabilir.
- Motor çağrılarında FreeType, çağıranın yığını 12 KB'tan azsa kendi 24 KB'lık
  yığınında çalışır.

AAText'in font algılaması (`otag.c`) `OT_Spec1_FontFile`'ı motordan bağımsız
okur; `"aatext"` motorlu fontlar da AAText tarafından tanınır.

Yeni fontların `"aatext"` motoruyla kurulması ve eskilerin dönüştürülmesi:
`ROADMAP.md`, madde 9 (font kurucu).

## 5. Kod sayfası

Sıra: `.otag`'taki `OT_Spec2_CodePage` → `ENV:ftcodepage` (512 bayt,
big-endian) → ISO-8859-1. AAText'in `SetEnvCodePage` mantığıyla birebir
uyumlu. AATextPrefs'te ayrı bir düğme yok: Gelişmiş sekmesinde seçilen
karakter seti (Latin-1…5, 9, 10, Windows-1250, ISO-8859-5, KOI8-R)
Kaydet/Kullan'da aynı biçimde `ENV:ftcodepage`'e (Kaydet'te `ENVARC:`'a da)
yazılır; Latin-1 seçilince değişken silinir. Kütüphane `ENV:ftcodepage`'i
her `OpenEngine`'de okur.

## 6. FREETYPE_PROPERTIES

`FT_Init_FreeType` sırasında `FREETYPE_PROPERTIES` ortam değişkeni okunur
(WinUAE'de doğrulandı). AAText `FT_New_Library` + `FT_Add_Default_Modules`
kullanırsa okunmaz; gerekirse `FT_Set_Default_Properties(lib)` (LVO -978).
AAText'in `hinting` ayarı `FT_Property_Set` ile bunu zaten ezer.

## 7. AAText'e faydalı olabilecek ek fonksiyonlar

| Fonksiyon | LVO | Olası kullanım |
|---|---|---|
| `FT_Get_Advance` / `FT_Get_Advances` | -912 / -918 | real metrics, TextLength hızlandırma |
| `FT_Request_Size` | -1050 | `real on` boyutunu büyük harf yüksekliğine göre istemek |
| `FT_Outline_Embolden`, `FT_GlyphSlot_Embolden` | -1056, -1002 | bold stili |
| `FT_GlyphSlot_Oblique` | -1008 | italic stili |
| `FT_Get_Kerning` | -192 | kerning (AAText bugün `kern` tablosunu kendisi okuyor) |
| `FT_Library_SetLcdFilter` | -726 | ileride alt piksel (LCD) çizim |
| `FT_Get_Var_Design_Coordinates` / `FT_Set_Var_Design_Coordinates` | -948 / -954 | variable fontlarda ağırlık seçimi |
| `FT_Error_String` | -1074 | debug sürümünde okunur hata mesajı |

Tam liste ve LVO'lar: `freetype2/src/aatext.funcs`, `freetype2/build/lvo.txt`.

---

## Durum (9.10.2026)

- WinUAE'de (OS 3.2.3, 68020 derlemesi) test edildi:
  - FreeType API: Verdana, Türkçe harfler, AA çizim.
  - Motor: freetype2.library 1.3 ile satır satır karşılaştırma (boylar, 191
    girişli genişlik listesi ve kerning aynı; 1 bit glifler daha düzgün),
    OTF/CFF 4 KB yığınla, Symbol.ttf, `FREETYPE_PROPERTIES`, kalın,
    bellek sızıntısı yok.
  - diskfont: `OT_Engine "aatext"` olan bir test fontu (`RAM:`'de, geçici
    `Assign LIBS: ... ADD` ile) `OpenDiskFont` ile açıldı, 11/13/16/24 punto.
  - Sistemin `LIBS:freetype2.library` 1.3'ü geri yüklendi; `LIBS:`'e
    aatext.library kurulmadı.
- **Henüz yapılmadı**: A1200/PiStorm'da denenmedi; AAText kütüphaneye
  geçirilmedi.
- Not: AAText çalışırken diskfont fontları ekranda AAText'in çizimiyle
  görünür; motorun kendi bitmap'ini görmek için `ft2dfont` her satırı bir de
  `tf_CharData`'dan `BltTemplate` ile çizer.

## Yol haritası için öneriler

1. AAText'i aatext.library'ye geçirmek (`ROADMAP.md`, madde 10): önce bir
   derleme seçeneği (`USE_AATEXTLIB=1`), statik yapıyla boyut ve hız
   karşılaştırması, sonra varsayılan.
2. Font kurucu (`ROADMAP.md`, madde 9): `"aatext"` motorlu `.otag`, kod
   sayfası içinde.
3. AGA aşaması (madde 7) için motorun `OT_GlyphMap8Bit` çıktısını
   değerlendirmek.
