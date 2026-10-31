# Egis MoC USB protokolü

Bu doküman EgisTec / LighTuning Match-on-Chip sensörlerin USB protokolünü anlatıyor. İlk kaynak, libfprint'in `egismoc` sürücüsü; o sürücü de Windows sürücüsünün USB trafiği dinlenerek çıkarılmış. Buradaki bilgiler gerçek cihazda doğrulandı:

- Cihaz: `1c7a:0587` ETU905A88-E (ASUS Zenbook 14 UX3402VA)
- Firmware: `9050.6.4.67`

✅ işareti, ilgili bilginin bu cihazda `FPAL_TRACE=1` dökümleriyle doğrulandığını gösterir.

## USB arayüzü

| Endpoint | Tip | Kullanım |
|---|---|---|
| EP0 | Control | Açılış dizisi |
| `0x02` | Bulk OUT | Komut gönderme |
| `0x81` | Bulk IN | Cevap okuma |
| `0x83` | Interrupt IN | Parmak dokunma olayı |

Arayüz `0`, sınıf `0xFF` (Vendor Specific).

## Açılış dizisi ✅

1. Interface 0 claim edilerek sensörün başka bir süreçte açık olmadığı kontrol edilir. Ardından interface bırakılır.
2. USB reset.
3. Interface 0 tekrar claim edilir.
4. Control IN istekleri:

| # | Tip | bRequest | wIndex | wLength | Anlamı |
|---|---|---|---|---|---|
| 1 | Vendor | `0x20` | 4 | 16 | Microsoft OS Descriptor: Extended Compat ID başlığı |
| 2 | Vendor | `0x20` | 4 | 40 | Aynısı, tam hali: `"WINUSB"` compatible ID |
| 3 | Standard | GET_STATUS | 0 | 2 | `01 00` (self-powered) |
| 4 | Standard | GET_STATUS | 0 | 2 | `01 00` |
| 5 | Vendor | `0x52` | 0 | 8 | `0a 0e 00 00 00 00 00 00` (anlamı bilinmiyor) |

İlk iki istek aslında sensöre özel bir şey değil. Windows bu standart mekanizmayla cihaza otomatik olarak WinUSB sürücüsünü bağlıyor; `0x20` cihazın Microsoft OS descriptor "vendor code"u.

## Çerçeve formatı ✅

```
Komut  (host → sensör):  45 47 49 53 00 00 00 01 | CK CK | payload...
Cevap  (sensör → host):  53 49 47 45 00 00 00 01 | ck ck | LEN(4, BE) | gövde... | SW1 SW2
```

- `LEN` alanı gövde ile durum kodunun (SW) toplam uzunluğudur ✅. libfprint firmware cevabındaki `0D` baytını "carriage return" sanıyordu; aslında bu bayt uzunluk alanı (13 = 11 karakterlik versiyon + 2 baytlık SW).
- Komut payload'ı 4 baytlık big-endian uzunlukla başlar. Ardından APDU benzeri gövde gelir: `50 INS P1 P2 [00 Lh Ll]`.
- Durum kodları:
  - `90 00` başarılı.
  - `90 04` eşleşme yok.
  - `91 00` "identify'a hazırlan" (arm) komutunun cevabı ✅.

### Checksum ✅

Hem komutlar hem cevaplar aynı checksum'u kullanır:

1. Checksum alanı `00 00` iken çerçeve big-endian 16 bitlik kelimeler halinde toplanır. Uzunluk tekse sona bir sıfır dolgu baytı eklenmiş gibi hesaplanır (bu bayt gönderilmez).
2. `CK = 0xFFFF − (toplam mod 0xFFFF)`.

Böylece checksum dahil bütün çerçevenin toplamı `0 (mod 0xFFFF)` olur. Sürücü checksum'ı tutmayan cevapları reddeder.

## Komutlar

| Komut | Payload | Gözlenen cevap |
|---|---|---|
| Firmware versiyonu | `00 00 00 07 50 7F 00 00 00 00 0C` | gövde: ASCII versiyon (`9050.6.4.67`) ✅ |
| Şablon listesi | `00 00 00 07 50 19 04 00 00 01 40` | gövde: N × 32 baytlık ID ✅ |
| Sensör reset | `00 00 00 04 50 1A 00 00` | SW `90 00` ✅ |
| Identify'a hazırlan | `00 00 00 04 50 17 01 01` | SW `91 00` ✅ |
| Sensör kontrolü | `00 00 00 04 50 17 02 00` | SW `90 00` ✅ |
| Eşleştirme ("check") | aşağıda | aşağıda ✅ |

**Silme komutu (`INS 0x18`) bu projede bilerek tanımlı değildir.**

### Eşleştirme komutu ✅

```
LEN(4) | 50 17 03 00 00 | IDLEN(2) | 32 × 00 | ID_1 ... ID_n | 00 40
```

- `IDLEN = (n + 1) × 32`: baştaki 32 sıfır baytlık "ayraç" da sayılır.
- `LEN = IDLEN + 9`.

Cevap:
- **Eşleşme var:** SW `90 00`, gövde 64 bayt.
  - İlk 32 bayt bilinmeyen meta veri. Örnek: `14 00 00 00 a0 00 c0 f0 03 00 00 00 03 00 00 00 58 ac ...`. Kalite veya skor olabilir.
  - Sonraki 32 bayt eşleşen ID (gövde offset 32, mutlak offset 46) ✅.
- **Eşleşme yok:** SW `90 04`.

## Parmak olayı (interrupt `0x83`) ✅

64 baytlık okuma isteğine 63 bayt döner (libfprint de bu bir baytlık eksikliği not etmiş). İçerik de kendi başına tam bir SIGE çerçevesidir:

```
53 49 47 45 00 00 00 01 2d 1e 00 00 00 31 | 17 01 20 ff ff ... ff | 90 00 90 00
```

Gövde `17 01` ile başlıyor; bu, "identify'a hazırlan" komutunun `INS P1` baytlarının yankısı. Sürücü şu an bu içeriği ayrıştırmıyor, sadece olayı bekliyor.

## Identify akışı ✅

```
list → reset → identify'a hazırlan → [parmak olayı] → sensör kontrolü → eşleştirme → reset
```

Sürücü eşleşen ID'nin `list` sonucunda gerçekten bulunduğunu kontrol eder. Bulunamazsa offset'in bu firmware için yanlış olduğu varsayılır ve hata döndürülür.

Windows'ta kayıtlı parmaklar listede görünür ve Linux'tan da eşleştirilebilir ✅.
