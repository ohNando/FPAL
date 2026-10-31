# FPAL

Linux için sıfırdan yazılmış parmak izi sürücüsü ve PAM modülü (Fingerprint Auth for Linux). Kullanıcı alanında çalışır.

- Dil: C17. Bağımlılık olarak sadece libc ve Linux kernel header'ları kullanılır. PAM modülü ek olarak libpam gerektirir.
- USB erişimi libusb olmadan, doğrudan `/dev/bus/usb` (usbdevfs) ioctl'leriyle yapılır.
- İlk desteklenen sensör: EgisTec / LighTuning `1c7a:0587` (Match-on-Chip, ASUS Zenbook 14 UX3402VA).

> ⚠️ Sürücü sensördeki kayıtlı şablonları silen hiçbir komut içermez. Windows'ta kayıtlı parmak izleri olduğu gibi kalır.

## Derleme

```sh
make          # build/fpal, build/libfpal.a ve build/pam_fpal.so (pam-devel gerekir)
make test     # donanım gerektirmeyen birim testleri
```

## Kullanım

```sh
sudo ./build/fpal info       # sensör bilgisi ve firmware versiyonu
sudo ./build/fpal list       # sensörde kayıtlı şablon ID'leri
sudo ./build/fpal identify   # parmağı okut, eşleşen ID'yi göster
sudo ./build/fpal bind $USER # okutulan parmağı kullanıcıya bağla
sudo ./build/fpal bindings $USER
sudo ./build/fpal unbind $USER   # sadece bilgisayardaki bağlamayı siler, sensöre dokunmaz
```

Bağlamalar `/var/lib/fpal/<kullanıcı>` dosyasında tutulur (root, 0600). Sensörde kayıt olmadığı sürece önce parmağı Windows Hello'da kaydetmek gerekir; Linux'tan kayıt (enroll) henüz yok.

PAM modülü ve güvenli test adımları: [docs/pam.md](docs/pam.md)

Protokol ayrıntıları: [docs/protocol-egis-moc.md](docs/protocol-egis-moc.md)

USB trafiğini görmek için komutların başına `FPAL_TRACE=1` eklenebilir. Bu, gönderilen ve alınan her paketi hex olarak yazdırır.

## Branch yapısı

- `develop`: aktif geliştirme dalı.
- `main`: sadece kararlı sürümler; release'ler buradan çıkar.

## Lisans

Apache-2.0. Ayrıntılar için [LICENSE](LICENSE) dosyasına bakın. Protokol bilgisi libfprint'in `egismoc` sürücüsü okunarak ve gerçek cihazda doğrulanarak elde edildi; libfprint'ten kod kopyalanmadı.
