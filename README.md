<div align="center">

# FPAL

**Fingerprint Auth for Linux**

Parmak izi sensörünü Linux'ta `sudo`, `su` ve giriş için kullan.
Sıfırdan yazılmış, bağımlılıksız, küçük ve okunabilir bir C projesi.

![License](https://img.shields.io/badge/license-Apache--2.0-blue)
![Language](https://img.shields.io/badge/language-C17-informational)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![Runtime](https://img.shields.io/badge/runtime-libc%20%2B%20libpam-success)
![Tests](https://img.shields.io/badge/tests-34%20passing-brightgreen)

</div>

```console
$ sudo true
[sudo] fingerprint for ohnando:
Fingerprint not recognised, try again
[sudo] fingerprint for ohnando:
$                                  # son denemede doğru parmak: şifre sorulmadan geçti
```

Parmak okunmazsa, sensör takılı değilse ya da SSH ile bağlanıldıysa sistem sessizce şifreye döner. Kilitli kalmazsın.

## Neden FPAL?

Dizüstü bilgisayarındaki parmak izi sensörü Windows'ta çalışıyor ama Linux'ta senin için bir şey yapmıyor. FPAL bu sensörle konuşan sürücüyü, komut satırı aracını ve PAM modülünü tek pakette verir.

- **Sıfırdan yazıldı.** Sensörün USB protokolü gerçek cihazda izlenip doğrulandı, her adımı [belgelendi](docs/protocol-egis-moc.md). Kod tabanı yaklaşık 2.500 satır C; baştan sona okunabilir.
- **Bağımlılık yok.** `libusb`, `libfprint`, D-Bus kütüphanesi ya da build sistemi gerekmez. Çalışma zamanında sadece libc ve PAM için `libpam` kullanılır. Derlemek için `make` yeter.
- **Windows kayıtlarına dokunmaz.** Parmak izlerini sensörden silen hiçbir komut kodda yok. Windows Hello'da kaydettiğin parmaklar Linux'ta olduğu gibi çalışır, çift boot kullanıyorsan ikisi birlikte yaşar.
- **Güvenli varsayılanlar.** Şifre her zaman yedek, sensöre yalnızca root erişir, bozuk cevaplar reddedilir, bağlama dosyaları symlink ve izin kontrollerinden geçer.
- **Çok sensöre hazır.** Sürücüler küçük bir arayüzün (`struct fpal_driver`) arkasında; yeni bir sensör eklemek çekirdeği değiştirmeden yeni bir klasör eklemek demek.

> [!NOTE]
> Parmak izi Linux'ta zaten [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) ve `fprintd` ile de kullanılabiliyor; olgun ve yaygın çözüm o. FPAL ondan farklı bir yol seçiyor: küçük, bağımlılıksız ve baştan sona anlaşılabilir olmak. Protokol bilgisi libfprint'in `egismoc` sürücüsünü referans alarak çıkarıldı, kod ise sıfırdan yazıldı.

## Özellikler

| | |
|---|---|
| 🔐 **sudo, su, login** | `pam_fpal.so` ile PAM kullanan her program parmak izi destekler |
| 🖐 **Kullanıcıya bağlama** | Her Linux kullanıcısına kendi parmakları bağlanır (`fpal bind`) |
| 🔍 **Şeffaf** | `FPAL_TRACE=1` ile sensörle konuşulan her USB paketini hex olarak görürsün |
| 🧪 **Test edilebilir** | 34 birim test, sahte USB ile donanım olmadan çalışır |
| 🧩 **Eklenebilir** | Yeni sensör = yeni sürücü klasörü, çekirdek aynı kalır |
| 🪶 **Hafif** | Arka planda çalışan bir servis yok; sensör sadece giriş sırasında açılır |

## Nasıl çalışır?

Sensörler iki türlüdür. Bazıları parmak izi görüntüsünü bilgisayara gönderir ve eşleştirmeyi yazılım yapar. FPAL'ın desteklediği **Match-on-Chip** sensörlerde ise şablonlar ve eşleştirme çipin içindedir: bilgisayar görüntüyü hiç görmez, çip sadece "eşleşti, şablon numarası şu" der.

```
  sudo / su / login
        │
        ▼
     libpam ──► pam_fpal.so ──► libfpal ──► usbdevfs ──► sensör
                    │               │                  (eşleştirme çipte)
                    │               └─ sürücü tablosu: egis_moc, …
                    ▼
        /var/lib/fpal/<kullanıcı>      hangi şablon kimin?
```

1. `sudo` PAM'a "bu kullanıcıyı doğrula" der.
2. `pam_fpal.so` sensörü açar ve parmağı bekler.
3. Çip, parmağı kendi içindeki şablonlarla karşılaştırır ve eşleşen şablonun numarasını döndürür.
4. Modül bu numaranın o kullanıcıya bağlı olup olmadığına bakar. Bağlıysa giriş onaylanır.

FPAL parmak izi **kaydı** yapmaz; Windows Hello'da kayıtlı parmaklarını çipten okuyup kullanıcıya bağlar (`fpal bind`).

## Hızlı başlangıç

```sh
sudo dnf install gcc make pam-devel     # Fedora; diğerleri için docs/usage.md
make && sudo make install

sudo fpal info                          # sensörü gör
sudo fpal bind $USER                    # parmağını okut ve kendine bağla
sudo scripts/authselect-fpal.sh install # sudo ve su artık parmak izi ister
```

Komutların tamamı, PAM ayarları ve sorun giderme: [docs/usage.md](docs/usage.md), [docs/pam.md](docs/pam.md)

## Durum

| | |
|---|---|
| ✅ | Sensörü görme, şablon listeleme, parmak tanıma, kullanıcıya bağlama |
| ✅ | `sudo` ve `su` ile PAM girişi |
| 🔜 | GNOME giriş ve kilit ekranı (fprintd'nin D-Bus arayüzünü taklit eden `fpald`) |
| 🔜 | Linux'tan parmak kaydı (isteğe bağlı ve ayrı bir bayrağın arkasında) |
| 🔜 | Daha fazla sensör ve Debian / Arch için paketler |

**Test edilen donanım:** EgisTec / LighTuning `1c7a:0587` (ASUS Zenbook 14 UX3402VA), firmware `9050.6.4.67`, Fedora 44, GNOME / Wayland.
Sürücü ailenin diğer üyelerini (`0582`–`0588`, `05a1`) tanır ama bunlar gerçek donanımda doğrulanmadı. Başka bir sensörün varsa deneyip sonucu paylaşman çok işe yarar.

## Güvenlik

- Parmak izi şifrenin **yerine değil, onu tamamlayan** (`sufficient`) bir yöntem olarak kullanılmalı. Match-on-Chip sensörde güven, USB'den gelen "eşleşti" cevabına dayanır; sensöre fiziksel USB erişimi olan biri bu cevabı taklit edebilir. Disk şifrelemesi (LUKS) için kullanılmamalı.
- Parmak izi ile girişte şifre girilmediği için GNOME Keyring açılmaz (fprintd ile de aynı sınırlama).
- Sensör yalnızca root tarafından açılabilir; herkese açık bir udev kuralı bilerek yazılmadı.

## Dokümantasyon

| | |
|---|---|
| [docs/usage.md](docs/usage.md) | Kurulum, komutlar, sorun giderme |
| [docs/pam.md](docs/pam.md) | PAM seçenekleri, mesajlar, authselect ile devreye alma |
| [docs/protocol-egis-moc.md](docs/protocol-egis-moc.md) | Sensörün USB protokolü, gerçek cihazda doğrulanmış |

## Katkı

Yeni sensör eklemek için `src/drivers/` altına bir klasör açıp `struct fpal_driver` tablosunu doldurmak yeterli; örnek olarak `src/drivers/egis_moc/` ve arayüz için `src/core/driver.h`. Hata bildirirken `sudo FPAL_TRACE=1 fpal info` çıktısını eklemen sorunu bulmayı çok kolaylaştırır.

Geliştirme `develop` dalında yapılır, `main` yalnızca sürümleri tutar. Commit mesajları `feat:`, `fix:`, `docs:`, `test:`, `build:`, `chore:` önekleriyle başlar.

## Lisans

[Apache-2.0](LICENSE). Protokol bilgisi libfprint'in `egismoc` sürücüsü okunarak ve gerçek cihazda doğrulanarak elde edildi; libfprint'ten kod kopyalanmadı.
