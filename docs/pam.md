# pam_fpx

`pam_fpx.so`, PAM kullanan her programa (sudo, su, login, GDM) parmak izi doğrulaması ekler.

```
auth  sufficient  pam_fpx.so  [timeout=ms] [retries=n] [debug]
```

| Seçenek | Varsayılan | Anlamı |
|---|---|---|
| `timeout=` | 10000 | Her denemede parmak için beklenecek süre (ms) |
| `retries=` | 3 | Yanlış parmakta kaç kez tekrar denenecek (en fazla 10) |
| `debug` | kapalı | Ayrıntılı mesajları auth log'a yazar |

## Dönüş değerleri

| Durum | Dönüş |
|---|---|
| Parmak eşleşti ve ID kullanıcıya bağlı | `PAM_SUCCESS` |
| Bütün denemelerde yanlış parmak | `PAM_AUTH_ERR` |
| Bağlama yok, sensör yok/meşgul, uzak oturum (`PAM_RHOST`), zaman aşımı | `PAM_AUTHINFO_UNAVAIL` |

Kullanıcının bağlaması yoksa sensör hiç açılmaz, şifreye hemen geçilir. Modül `sufficient` olarak kullanılmalıdır; böylece parmak izi tutmazsa şifre her zaman çalışır.

## Güvenli deneme

PAM dosyalarıyla oynarken **ayrı bir terminalde `sudo -i` ile root shell açık tut**. Bir hata olursa oradan geri alırsın.

1. Önce parmağı kullanıcıya bağla:

   ```sh
   sudo ./build/fpx bind $USER
   ```

2. Sistemin geri kalanına dokunmayan bir test servisi oluştur. Modül kurulmadan doğrudan `build/` içinden yüklenir:

   ```sh
   printf 'auth     sufficient %s/build/pam_fpx.so debug\nauth     required   pam_deny.so\naccount  required   pam_permit.so\n' \
       "$PWD" | sudo tee /etc/pam.d/fpx-test
   sudo pamtester fpx-test $USER authenticate
   ```

   Bağlı parmakla `successfully authenticated`, başka parmakla `Authentication failure` görmelisin.

3. Log'lar:

   ```sh
   sudo journalctl -b --no-pager | grep pam_fpx
   ```

4. Test servisini kaldır: `sudo rm /etc/pam.d/fpx-test`

sudo ve giriş ekranına geçiş (authselect profili, `fprintd` mask) bir sonraki adımda anlatılacak.
