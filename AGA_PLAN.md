# AGA ekranlar için yumuşatma — ön hazırlık

Durum: **plan, onay bekliyor.** Kod değişikliği yok; yalnızca test araçları
hazırlandı. Dal: `aga`. Yedek: `Desktop\Afa_backup_0.19` (0.19, tüm git
geçmişi).

CLAUDE.md'deki 6. aşama: paletli ekranlarda (8 bit ve altı) birkaç ara tonla
yumuşatma; yeterli renk yoksa orijinal `Text()`'e dönüş.

---

## 1. Bugünkü davranış

| Durum | Sonuç |
|---|---|
| Güvenli ölçü modu, derinlik < 15 | `R_DEPTH`: orijinal `Text()`, bitmap font |
| Gerçek ölçü modu (`real on`), paletli ekran | 1 bitlik `BltTemplate`: TrueType harf biçimi, ara ton yok |
| RTG 8 bit (LUT8) | aynı: `R_LUT8` / 1 bit |

Paletli ekranda AAText bugün hiçbir şeyi bozmuyor; yalnızca yumuşatmıyor.

## 2. Temel sorun

RTG'de her piksel için "arka plan rengi + yazı rengi × kaplama" hesaplanıp
doğrudan RGB olarak yazılıyor. Paletli ekranda piksel bir **kalem numarası**:
karışım rengini paletteki bir kaleme çevirmek gerekiyor ve paletteki renk
sayısı az (Workbench'te genellikle 4–16, en çok 256).

## 3. Seçenekler ve önerim

### 3.1 Ara tonların rengi

**A. Paletteki en yakın renk (öneri).** Her (arka plan kalemi, yazı kalemi,
kaplama seviyesi) üçlüsü için karışım rengini hesapla, paletteki en yakın
kalemi bul (`FindColor()`, graphics V39). Kalem ayırmaz, her ekranda
çalışır, ekran kapanınca bırakılacak bir şey yoktur. Sonucun kalitesi
palete bağlı: Workbench'in gri tonları varsa iyi.

- Uygun renk yoksa (en yakın kalem, iki uca olan uzaklıktan daha uzaksa) o
  seviye kullanılmaz; o piksel tam yazı rengi ya da arka plan olur. Hiç
  uygun ara ton yoksa harf 1 bit çizilir (bugünkü gibi), bozulmaz.
- Önbellek: anahtar kalem numarası değil **RGB** (yazı ve arka plan rengi)
  olur; palet değişirse (`SetRGB32`) eski kayıtlar kendiliğinden geçersiz
  kalır. Küçük bir tablo (ör. 64 kayıt), ekran başına.

**B. `ObtainBestPen()` ile ara ton ayırmak.** Daha iyi sonuç, ama paylaşılan
kalemleri tüketir ve ekran kapanmadan `ReleasePen()` gerekir. AAText
ekranların kapanışını izlemiyor; yanlış yapılırsa kalem sızıntısı ya da
kapanmayan ekran olur. **Şimdilik yapılmasın**; A yetmezse yalnızca
Workbench ekranı için ayrı bir aşama olarak ele alınır.

### 3.2 Kaç ton

Kaplama 4 seviyeye bölünür: 0 (dokunma), ⅓, ⅔, 1 (tam yazı rengi). Yani 2
ara ton. 256 renkli ekranlarda 3 ara ton da denenebilir (ayar değil, sabit
başlangıç).

### 3.3 Okuma ve yazma

- **JAM2** (arka plan kalemi belli): okuma gerekmez. Her piksel üç kalemden
  biri: arka plan, ara tonlar, yazı.
- **JAM1** (alttaki her neyse): alttaki pikseller okunmalı, çünkü arka plan
  her pikselde farklı olabilir (ikon etiketleri, desenli zemin).
- İlk sürüm: hücreyi `ReadPixelArray8()` ile oku, kalem numaralarını tabloyla
  karıştır, `WritePixelArray8()` ile yaz. İkisi de geçici bir RastPort ve
  bitmap ister; bunlar her çağrıda değil, bir kez ayrılır.
- Hız sorun olursa ikinci adım: tam kaplanmış pikseller `BltTemplate()` ile
  (blitter, hızlı), yalnızca ara tonlu kenar pikselleri tek tek. Harflerin
  piksellerinin yaklaşık üçte biri kenar.

### 3.4 Kırpma

`WritePixelArray8()` RastPort'un katmanı üzerinden kırpar (doğrulanmadı).
`ReadPixelArray8()` örtülü bölgeler için ne döndürür, doğrulanmadı; yazma
zaten kırpıldığı için görünür sonuç doğru olmalı. Test listesinde.

### 3.5 Ölçü modları

İki mod da aynı karıştırma kodunu kullanır:
- Güvenli mod: bugün `R_DEPTH`'te dönülen yer.
- Gerçek ölçü modu: bugünkü 1 bitlik yedek çizimin yerine, mümkünse
  yumuşatılmış; olmazsa yine 1 bit.

### 3.6 Ayar

`aga on|off`. İlk sürümde **varsayılan kapalı**; AATextPrefs Gelişmiş
sekmesinde bir kutu. Deneyler iyi giderse sonraki sürümde varsayılan açık.

## 4. Riskler

- **Hız:** planar ekranda okuma/yazma yavaş, chip RAM. Hızlandırıcısız
  68020'de belirgin yavaşlama beklenir. PiStorm'da sorun beklenmez
  (doğrulanmadı).
- **Az renkli paletler:** 4 renkli Workbench'te ara ton bulunmayabilir;
  o zaman sonuç bugünkünden kötü olmamalı (1 bit).
- **Renk değişen ekranlar** (renk döngüsü, oyunlar): RGB anahtarlı önbellek
  bunu karşılar ama her değişiklikte tablo yeniden hesaplanır.
- **Chip RAM:** geçici bitmap'ler chip RAM ister; boyutları satır başına
  sınırlı tutulur.

## 5. Test planı

Araçlar (bu hazırlıkta eklendi):
- `tests/pubscreen.c`: altıncı argüman **derinlik**. Örnek:
  `pubscreen 640 256 topaz.font 8 300 4` → 16 renkli AATEST ekranı.
- `tests/texttest.c`: altıncı argüman **public screen**. Örnek:
  `texttest calibriregular.font 16 30 - - AATEST`.
- Konsol: `NewShell "CON:0/20/600/200/Test/SCREEN AATEST"`.

Denenecekler: derinlik 2, 3, 4, 8; JAM1 ve JAM2; ikon etiketleri (AGA
Workbench); menüler; örtülü pencereler; kaldırma (`AAText QUIT`); 68020
hızı (JIT kapalı WinUAE) ve A1200/PiStorm.

**Henüz yapılmadı:** bugünkü davranışın AGA ekranda ölçümü (WinUAE
kapalıydı). Uygulamaya başlamadan önce ilk iş bu.

## 6. Aşamalar (öneri)

1. **A1:** renk tablosu (en yakın kalem, RGB anahtarlı önbellek) + JAM2,
   güvenli mod. vamos testi: tablonun doğruluğu.
2. **A2:** JAM1 (`ReadPixelArray8`), örtülü pencereler.
3. **A3:** gerçek ölçü modu (1 bitlik yedek çizimin yerine).
4. **A4:** hız: `BltTemplate` + kenar pikselleri; 68020 ve PiStorm ölçümü.
5. **A5:** `aga` ayarı, AATextPrefs kutusu, belgeler, sürüm.

## 7. Doğrulanmadı

- `FindColor()`'un OS 3.2'de bütün kalemleri mi, yalnızca paylaşılanları mı
  aradığı (`maxpen` argümanı).
- `ReadPixelArray8`/`WritePixelArray8`'in katman kırpması ve örtülü
  bölgeler.
- RTG LUT8 ekranlarda aynı yolun çalışması (CGX'in 8 bit karşılıkları da
  var).
- Hız.
