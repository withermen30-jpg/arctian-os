# Derleme ve çalıştırma

## Gerekenler

Derleme Linux araç zinciri ister (gcc'nin freestanding çıktı vermesi, `ld`/`objcopy` ile
`elf_x86_64` bağlanması, `xorriso` ile ISO üretimi). Windows'ta desteklenen yol **WSL
üzerinden Debian**'dır.

```sh
sudo apt update
sudo apt install -y build-essential nasm binutils python3 xorriso file
sudo apt install -y qemu-system-x86            # denemek istersen
```

| Araç | Ne için |
|---|---|
| `gcc` (freestanding) | tüm C kodu; `-nostdlib` ile bağlanır |
| `nasm` | `boot/*.asm` önyükleyici aşamaları |
| `ld`, `objcopy` | modül ELF'leri → düz `.bin`; `--wrap=lv_obj_send_event` |
| `python3` | paketleyici, imzacı, font/duvar kağıdı üreticileri |
| `xorriso` | El Torito önyüklenebilir ISO |
| `qemu-system-x86_64` | çalıştırma / kurulum testi (isteğe bağlı) |

Üçüncü parti bağımlılık gerekmez: LVGL, mbedTLS, QuickJS ve Lexbor depoya **vendored**
edilmiştir ([third_party/README.md](../third_party/README.md)).

## Windows: tek adım

```bat
build.bat
```

Betik şunları yapar: WSL Debian dağıtımının var olduğunu doğrular, proje kökünü `wslpath` ile
çevirir, `make -j4` çalıştırır ve **çıktıyı doğrular**: disk imajı var mı, ISO bu derlemeden
sonra yenilenmiş mi (işaret dosyasıyla karşılaştırır). RC=0 tek başına yeterli sayılmaz; ISO
eksikse `[HATA]` ile RC=1 döner.

## Linux / WSL: doğrudan make

```sh
make            # tüm modüller + imaj + ISO  (varsayılan hedef: all -> pack)
make qemu-iso   # ISO'dan önyükleme (CD-ROM)
make qemu       # disk imajından önyükleme
make debug      # kurulum + hesap ekranlarını atlayan geliştirme imajı (arctian-debug.iso)
make setuptest  # kurulum özelleştirme ekranını doğrudan açan imaj
make clean      # build/ silinir
```

Çıktılar:

```
build/arctian.img   ham disk imaji (BIOS'tan doğrudan önyüklenebilir)
build/arctian.iso   El Torito önyüklenebilir ISO (USB / CD / kalıp)
build/obj-<hash>/   nesne dosyaları (bayrak kümesinin md5 ön eki)
```

Nesne dizini derleme bayraklarına göre adlandırılır (`FLAGS_HASH`): `DEBUG=1` ile `DEBUG=0`
aynı anda bozulmadan durur, karışık nesnelerden imaj üretilmez.

## Derleme bayrakları

| Değişken | Etkisi |
|---|---|
| `DEBUG=1` | açılış logları + otomatik kullanıcı (kurulum/hesap ekranları atlanır) |
| `SETUP_TEST=1` | kurulum özelleştirme ekranı doğrudan açılır |
| `T06_TEST=1` | 0.6 öz-testleri imaja girer |
| `T07_TEST=1` | 0.7 öz-testleri imaja girer |
| `EXTRA_CFLAGS=...` | ek derleme bayrakları |

```sh
make pack DEBUG=1 T07_TEST=1     # öz-test imajı; seri porta OK/HATA yazar
make -j4 DEBUG=0                 # sürüm imaji
```

Sürüm numarası `version.json`'dan okunur ve ayrı bir bayrakla (`VERSION_CFLAGS`) gömülür;
komut satırından `EXTRA_CFLAGS` verilse bile sürüm `0.0`'a düşmez.

## Üretileni doğrulama

```sh
file build/arctian.iso                 # ISO-9660 boot image, El Torito
isoinfo -d -i build/arctian.iso        # CD001, boot catalog 0100, BOOTABLE
ls -l build/arctian.img build/arctian.iso
```

Paketleyici her modülü taze `.bin` ile karşılaştırıp LBA konumlarını yazar; imaj boyutu ve ISO
boyutu ekrana basılır. ISO üretilemezse derleme **durur** (yarım/0 baytlık çıktı bırakmaz).

## QEMU'da denemek

```sh
qemu-system-x86_64 -cdrom build/arctian.iso -boot d -m 512M -vga std -serial stdio
qemu-system-x86_64 -drive file=build/arctian.img,format=raw -m 512M -vga std
```

Kurulum akışını uçtan uca test etmek için:

```sh
bash tools/test_install.sh      # ISO + 64MB sanal hedef disk ile QEMU başlatır
bash tools/run_installed.sh     # kurulu diski (build/target.img) doğrudan açar
```

`-serial stdio` verirse açılış logları ve öz-test satırları (`OK` / `HATA`) terminale düşer.

## Fiziksel diske yazma

```sh
python3 tools/arctian_writer.py --list
python3 tools/arctian_writer.py --disk /dev/sdb --yes
```

**Dikkat:** diske yazılacak dosya `build/arctian.img` olmalıdır. ISO'nun LBA 0'ında `0x55AA`
önyükleme imzası yoktur; disk olarak yazıldığında BIOS onu önyükleyemez.

## Uygulama ve SDK derleme

```sh
cd sdk/examples/merhaba && ../../wf build merhaba     # .wlf üretir
bash tools/build_wlf_examples.sh                      # tüm örnekler -> build/wlf/
```

`wf`, `gcc` + ELF bağlayıcısı kullandığı için **Windows'ta yerel olarak derlemez**; Linux veya
WSL tarafında çalıştır (`wf.cmd` WSL'e köprüler). Ayrıntı: [sdk/README.md](../sdk/README.md).

## Sık karşılaşılan tuzaklar

- **`make` hiçbir şey yapmadan başarılı dönüyorsa:** Makefile başındaki `.DEFAULT_GOAL := all`
  satırı bunun ilacıdır. `-include $(find ... -name '*.d')` satırı `all:` hedefinden önce
  durduğunda GNU Make 4.x `.d` dosyasındaki ilk hedefi varsayılan hedef sayar ve hedefsiz
  çağrılar "is up to date" deyip çıkar. `-include` kapsamı da yalnızca `$(BUILD)` olmalıdır;
  SDK emulatörünün göreli yollu `.d` dosyaları "No rule to make target" hatası verir.
- **`build.bat` karakter yutuyorsa (`echo` → `cho`):** betik CRLF satır sonuna sahip olmalıdır;
  LF-only bir `.bat` cmd içinde satır başlarını yer.
- **Bayrak değişti ama kod derlenmedi gibi görünüyor:** nesne dizini bayrağa göre
  adlandırılmıştır (`build/obj-<hash>`); eski dizindeki nesneler kullanılmaz, boşuna `make clean`
  atma.
- **ISO yok ama derleme "BAŞARILI" dedi:** `xorriso` kurulu değilse paketleyici hatayı artık
  yutmaz, derlemeyi durdurur. `apt install xorriso`.
- **Windows'ta `.wlf` derlenemiyor:** beklenen davranış (ELF bağlayıcısı yok); WSL kullan.
