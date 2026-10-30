# OpenFinger

Linux için sıfırdan yazılmış, kullanıcı alanında çalışan parmak izi sürücüsü ve PAM modülü.

- Dil: C17. Bağımlılık olarak sadece libc ve Linux kernel header'ları kullanılır. PAM modülü ek olarak libpam gerektirir.
- USB erişimi libusb olmadan, doğrudan `/dev/bus/usb` (usbdevfs) ioctl'leriyle yapılır.
- İlk desteklenen sensör: EgisTec / LighTuning `1c7a:0587` (Match-on-Chip, ASUS Zenbook 14 UX3402VA).

> ⚠️ Sürücü sensördeki kayıtlı şablonları silen hiçbir komut içermez. Windows'ta kayıtlı parmak izleri olduğu gibi kalır.

## Derleme

```sh
make          # build/fpx ve build/libfpx.a
make test     # donanım gerektirmeyen birim testleri
```

## Kullanım

```sh
sudo ./build/fpx info       # sensör bilgisi ve firmware versiyonu
sudo ./build/fpx list       # sensörde kayıtlı şablon ID'leri
sudo ./build/fpx identify   # parmağı okut, eşleşen ID'yi göster
```

USB trafiğini görmek için komutların başına `FPX_TRACE=1` eklenebilir. Bu, gönderilen ve alınan her paketi hex olarak yazdırır.

## Branch yapısı

- `develop`: aktif geliştirme dalı.
- `main`: sadece kararlı sürümler; release'ler buradan çıkar.
