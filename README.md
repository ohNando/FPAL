# FPAL

**Fingerprint Auth for Linux**: Match-on-Chip parmak izi sensörleri için sıfırdan yazılmış, kullanıcı alanında çalışan sürücü, komut satırı aracı ve PAM modülü. `sudo` ve `su` parmak izinle açılır, şifre her zaman yedek olarak kalır.

- **Dil:** C17. Çalışma zamanında sadece libc ve Linux çekirdeğinin kendi arayüzleri kullanılır. PAM modülü ek olarak `libpam` ister.
- **Libusb yok:** sensörle doğrudan `/dev/bus/usb` (usbdevfs) ioctl'leri üzerinden konuşulur.
- **Çok sensörlü mimari:** sürücüler bir tabloya eklenir (`src/drivers/`); şu an tek sürücü var.
- **Güvenli:** sensördeki şablonları silen hiçbir komut kod tabanında yok. Windows'ta kayıtlı parmak izlerin olduğu gibi kalır.

## Durum

| | |
|---|---|
| Çalışıyor | Sensörü görme, şablonları listeleme, parmak tanıma, kullanıcıya bağlama, `sudo`/`su` ile PAM girişi |
| Henüz yok | GNOME giriş ve kilit ekranı (kendi D-Bus servisi `fpald` ile gelecek), Linux'tan parmak kaydı (enroll) |
| Test edilen donanım | EgisTec / LighTuning `1c7a:0587` (ASUS Zenbook 14 UX3402VA), firmware `9050.6.4.67` |
| Test edilen sistem | Fedora 44, GNOME / Wayland |

Başka sensörler ve dağıtımlar henüz denenmedi. `egis_moc` sürücüsü `0582`–`0588` ve `05a1` kimliklerini tanır, ancak bunlar gerçek donanımda doğrulanmadı.

## Kurulum

Derleme için C derleyicisi, `make` ve `libpam` başlıkları gerekir:

```sh
sudo dnf install gcc make pam-devel      # Fedora
sudo apt install gcc make libpam0g-dev   # Debian / Ubuntu (denenmedi)

make            # build/fpal, build/libfpal.a, build/pam_fpal.so
make test       # donanım gerektirmeyen birim testleri
sudo make install
```

`make install` `fpal` komutunu `/usr/local/sbin` altına, `pam_fpal.so` dosyasını dağıtımın PAM klasörüne kurar. Yollar `PREFIX=` ve `PAMDIR=` ile değiştirilebilir.

## Kullanım

Sensör yalnızca root tarafından açılabilir (bilerek: herkese açık bir udev kuralı olsaydı her program sensöre ham komut gönderebilirdi).

```sh
sudo fpal info            # sensör bilgisi ve firmware versiyonu
sudo fpal list            # sensörde kayıtlı şablon ID'leri
sudo fpal identify        # parmağı okut, eşleşen ID'yi göster
sudo fpal bind $USER      # okutulan parmağı kullanıcıya bağla
sudo fpal bindings $USER  # kullanıcıya bağlı ID'ler
sudo fpal unbind $USER    # sadece bilgisayardaki bağlamayı siler, sensöre dokunmaz
fpal version
```

Linux'tan parmak kaydı henüz yok; parmağını önce Windows Hello (ya da sensörün kendi aracı) ile kaydetmen gerekir. Çip bu şablonları Linux'ta da eşleştirir. Bağlamalar `/var/lib/fpal/<kullanıcı>` dosyasında tutulur (root, `0600`).

### sudo ve su ile giriş

```sh
sudo fpal bind $USER
sudo scripts/authselect-fpal.sh install   # Fedora / RHEL (authselect)
```

Betik `fprintd`'yi kapatıp `custom/fpal` adlı bir authselect profili seçer; geri almak için `revert` kullanılır. Ayrıntılar, seçenekler (`timeout=`, `retries=`, `quiet`, `debug`) ve güvenli deneme adımları: [docs/pam.md](docs/pam.md)

```
$ sudo true
[sudo] fingerprint for ohnando:
Fingerprint not recognised, try again
[sudo] fingerprint for ohnando:
```

Parmak okunmazsa ya da sensör yoksa şifre sorulur.

### Sorun giderme

`FPAL_TRACE=1` gönderilen ve alınan her USB paketini hex olarak yazdırır, `FPAL_DEBUG=1` ayrıntılı mesajlar verir:

```sh
sudo FPAL_TRACE=1 fpal identify
```

Sensör başka bir süreçte açıksa (ör. `fprintd`) `busy` hatası alırsın: `sudo systemctl mask --now fprintd`.

## Güvenlik notları

- Match-on-Chip sensörde eşleştirme çipin içinde yapılır; ana bilgisayar sadece "eşleşti + şablon ID'si" cevabını görür. Sensöre fiziksel USB erişimi olan biri bu cevabı taklit edebilir. Bu yüzden parmak izi şifrenin yerine değil, onu **tamamlayan** (`sufficient`) bir yöntem olarak kullanılmalı; disk şifrelemesi (LUKS) için uygun değildir.
- Parmak izi ile girişte şifre girilmediği için GNOME Keyring açılmaz (fprintd ile de aynı sınırlama). Açılıştan sonraki ilk girişte şifre gerekir.
- Cevaplar checksum ile doğrulanır; bozuk cevap hata sayılır. Bağlama dosyalarına yalnızca root'a ait, başkalarının yazamadığı ve symlink olmayan dosyalar güvenilir.
- SSH gibi uzak oturumlarda parmak izi istenmez.

## Proje yapısı

```
include/fpal/   genel API (fpal.h, config.h, types.h, bindings.h)
src/core/       sürücü tablosu, cihaz yönetimi, log, trace, kilit
src/transport/  usbdevfs (gerçek USB)
src/drivers/    sensör sürücüleri (egis_moc)
src/storage/    kullanıcı <-> şablon bağlamaları
pam/            pam_fpal.so
tools/          fpal komutu
tests/          birim testleri (sahte USB ile)
docs/           protokol ve PAM dokümanları
scripts/        authselect geçiş betiği
```

Tüm ayarlanabilir değerler (zaman aşımları, yollar, limitler) `include/fpal/config.h` içindedir ve derlerken değiştirilebilir:

```sh
make CFLAGS_EXTRA='-DFPAL_PAM_TIMEOUT_MS=5000'
```

Yeni bir sensör eklemek için `struct fpal_driver` tablosunu dolduran bir sürücü yazılır; bkz. `src/core/driver.h`.

Protokol ayrıntıları (gerçek cihazda doğrulanmış): [docs/protocol-egis-moc.md](docs/protocol-egis-moc.md)

## Geliştirme

- `develop`: aktif geliştirme dalı.
- `main`: yalnızca kararlı sürümler; release'ler buradan çıkar.
- Commit mesajları `feat:`, `fix:`, `docs:`, `test:`, `build:`, `chore:` önekleriyle başlar.

## Lisans

Apache-2.0, bkz. [LICENSE](LICENSE). Protokol bilgisi libfprint'in `egismoc` sürücüsü okunarak ve gerçek cihazda doğrulanarak elde edildi; libfprint'ten kod kopyalanmadı.
