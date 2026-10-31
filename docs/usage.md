# Kurulum ve kullanım

## Derleme ve kurulum

Derleme için C derleyicisi, `make` ve `libpam` başlıkları gerekir:

```sh
sudo dnf install gcc make pam-devel      # Fedora
sudo apt install gcc make libpam0g-dev   # Debian / Ubuntu (denenmedi)

make            # build/fpal, build/libfpal.a, build/pam_fpal.so
make test       # donanım gerektirmeyen birim testleri
sudo make install
```

`make install` `fpal` komutunu `/usr/local/sbin` altına, `pam_fpal.so` dosyasını dağıtımın PAM klasörüne kurar. Yollar `PREFIX=` ve `PAMDIR=` ile değiştirilebilir.

Tüm ayarlanabilir değerler (zaman aşımları, yollar, limitler) `include/fpal/config.h` içindedir ve derlerken değiştirilebilir:

```sh
make CFLAGS_EXTRA='-DFPAL_PAM_TIMEOUT_MS=5000'
```

## Komutlar

Sensör yalnızca root tarafından açılabilir. Bu bilerek böyle: herkese açık bir udev kuralı olsaydı kullanıcının çalıştırdığı her program sensöre ham komut gönderebilirdi.

| Komut | Ne yapar |
|---|---|
| `sudo fpal info` | Sensör, sürücü, seri numarası ve firmware versiyonu |
| `sudo fpal list` | Sensörde kayıtlı şablon ID'leri |
| `sudo fpal identify [-t ms]` | Parmağı okut, eşleşen ID'yi göster |
| `sudo fpal bind [-t ms] KULLANICI` | Okutulan parmağı bir Linux kullanıcısına bağla |
| `sudo fpal bindings KULLANICI` | Kullanıcıya bağlı ID'leri göster |
| `sudo fpal unbind KULLANICI` | Bağlamaları sil. Yalnızca bilgisayardaki dosyayı siler, sensöre dokunmaz |
| `fpal version` | Sürümü göster |

Linux'tan parmak kaydı henüz yok: parmağı önce Windows Hello (ya da sensörün kendi aracı) ile kaydetmek gerekir. Çip bu şablonları Linux'ta da eşleştirir. Bağlamalar `/var/lib/fpal/<kullanıcı>` dosyasında tutulur (root, `0600`).

## sudo ve su ile giriş

```sh
sudo fpal bind $USER
sudo scripts/authselect-fpal.sh install   # Fedora / RHEL (authselect)
```

Betik `fprintd`'yi kapatıp `custom/fpal` adlı bir authselect profili seçer; geri almak için `revert` kullanılır. PAM seçenekleri (`timeout=`, `retries=`, `quiet`, `debug`), mesajlar ve güvenli deneme adımları için: [pam.md](pam.md)

## Sorun giderme

`FPAL_TRACE=1` gönderilen ve alınan her USB paketini hex olarak yazdırır, `FPAL_DEBUG=1` ayrıntılı mesajlar verir:

```sh
sudo FPAL_TRACE=1 fpal identify
```

| Belirti | Sebep ve çözüm |
|---|---|
| `device or resource busy` | Sensörü başka bir süreç (ör. `fprintd`) açmış: `sudo systemctl mask --now fprintd` |
| `permission denied` | Komut root olarak çalışmalı: `sudo fpal ...` |
| `NO MATCH` | Parmak sensörde kayıtlı değil. Önce Windows Hello'da kaydet |
| sudo parmak izi sormuyor | Kullanıcıya parmak bağlanmamış (`sudo fpal bindings $USER`) ya da authselect profili seçilmemiş (`authselect current`) |
