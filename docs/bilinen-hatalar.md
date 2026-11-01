# Bilinen Hatalar

0.7 üzerinde yaptığım geniş bir kod taramasının sonucu. Repoyu inceleyenlerin
"burası düzgün mü?" diye tahmin yürütmesine gerek olmasın diye yazdım; burada
listelenen her madde kod okunarak bulundu, çoğu QEMU'da ya da kodu izleyerek
doğrulandı. Düzelttikçe buradan düşürüyorum.

Numaraları issue'larda referans vermek için kullanabilirsiniz.

## Son durum (Kasım 2026)

Listeyi baştan sona elden geçirdim; maddelerin sırası ve numaraları
bilerek değişmedi. Durum şöyle:

Düzeltildi: 1, 2, 3, 5, 7, 8, 9, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21,
22, 25, 29, 30, 33, 38, 39, 40, 41, 42, 43, 44, 46, 47, 48, 49, 50, 51,
52, 54, 55, 56, 57, 58, 59, 61, 62, 64, 65, 66, 71, 72, 73, 74, 75, 76.

Bilinçli olarak ertelenenler (gerekçeleriyle):

- 4: şifreleme kurulumunda geri alma, iş bölümüne göre ayrı bir tasarım
  işi; yarım çözüm şu ankinden daha tehlikeli olurdu.
- 6: AHCI/ATA tek DMA tamponu; gerçek kilit mekanizması ve çoklu slot
  ister, tek oturumda güvenli şekilde sığdırılamadı.
- 10: AFS tutamaklarına nesil sayacı eklemek dosya sistemi formatı
  değişikliği istiyor.
- 15: günlüğe hiç girmeyen büyük okumalarda baypas yolu; doğrulaması
  gerçek donanım/uzun test ister, tek başına dokunmak riskliydi.
- 23 ve 24: kullanıcı aralığının sayfa tablosundan denetlenmesi ve
  süreç başına fd tablosu, çekirdeğin bellek ayracıyla birlikte
  ele alınması gereken işler; 70'teki `SYS_GETPID` de aynı kapsamda.
- 26: kilit ekranı perde olmaktan çıkmalı; oturum/kabuk yaşam döngüsü
  tasarımı gerektiriyor.
- 27: çerez izolasyonu tarayıcı uygulamalarında yapı işi.
- 28: DNS xid'i sıralı, kaynak portu sabit, DHCP yanıtı MAC'ten türeyen
  sabit xid'e bakıyor; rastgelelik kaynağı ağ yığınına bağlanmalı.
- 31 ve 32: TCP yeniden gönderim ve akış kontrolü baştan yazılmalı;
  acele düzeltme bağlantıları daha kötü bozar.
- 34: poll koruması erken dönüşü sürdürüyor; ARP çözümlemesini poll
  kuyruğuna almak ipv4 katmanının yeniden düzenlenmesini istiyor.
- 35: realloc'un içeriği kopyalamaması 25'le birlikte düzeltildi;
  WANT_WRITE livelock yolu ayrıca duruyor.
- 36 ve 37: .wlf paketi tek ortak tamponda okunmaya, host tek oturumla
  çalışmaya devam ediyor; ikisi birden elletilmeden düzelmez.
- 53: tablo dışı okuma kısmı düzeltildi; S3 çağrısı bootloader
  desteği olmadığı için duruyor.
- 60: AFS izin modelinin doğrulanması; tüm disk tek kullanıcının
  (`C:/`) ve her süreç uid 1000 ile çalıştığı için bugünkü POSIX
  denetimi yazılsa bile gerçek bir koruma sağlamaz. Önce kimlik
  modeli netleşmeli.
- 63: yazma yolundaki 32 bitlik taşmalar giderildi; kapasite
  alanının uint64'e çıkarılması yine format değişikliği istiyor.
- 67: kurulum sonrası uzantısız yol denemesi; kayıt eşleşme
  mantığının tamamıyla ele alınmalı.
- 68: `update_apply`'ın sürüm işaretlemesi, denetleme sonucu
  saklanmadan anlam kazanmıyor.
- 69: derlemeye girmeyen `desktop/shell/desktop.c` ve `cursor.c`;
  canlandırmak mı silmek mi kararı verilmedi.
- 70: 23, 24, 31 ve 32'nin gölgesinde duruyor.

Düzeltilenlerin her biri ayrı commit'te; commit mesajında maddenin
kendisi yazılı, numara verilmedi.

## Veri kaybı ve bozulma

Bunlar en tehlikeli olanlar; sessiz çalışırlar, kullanıcı bir şey fark etmez.

1. `afs_write_handle` her çağrıda dosyanın tamamını yeniden yazar; append diye
   bir şey yok. Ama üç yer bunu umursamadan üst üste `write()` çağırıyor:
   tarayıcı yerimleri (`desktop/apps/browser/main.c:5448` `bm_save`), asistanın
   günlük dosyası (`desktop/assistant.c:115`) ve asistanın "not al" özelliği
   (`desktop/assistant.c:331`). Sonuç: `yerimleri.cfg` diye 1 baytlık `"\n"`
   dosyası kalıyor, `asistan.log` da öyle, not da öyle. Üstelik "kaydedildi"
   mesajı gösterilmeye devam ediyor.
2. Çöp kutusundan geri yükleme 128 KB'la sınırlı tampon kullanıyor ve sonra
   kaynağı siliyor (`desktop/apps/trash/main.c:132`). 128 KB üstü dosya
   geri yüklenirken kırpılır, orijinali de gider.
3. Terminalde `mv` hedef volume'e taşamadığında kopyala-sil yoluna düşüyor ama
   boyut kontrolü yok (`desktop/apps/terminal/main.c:711`); 512 KB üstü dosya
   hedefte eksik yazılır, kaynak silinir. `cp`'de bu kontrol var, `mv`'de unutulmuş.
4. Disk şifreleme kurulumu önce veriyi şifreleyip header'ı en son yazıyor
   (`kernel_plus/system/diskcrypt.c:188`). Bu pencerede olacak bir çökme ya da
   tek bir bozuk sektör, diski "şifresiz ama verisi şifreli" bırakır; açılışta
   AFS header'ı bozuk görür ve `afs_format()` ile diski sıfırlar. Hata yollarında
   de geri al yok.
5. `storage_read`/`storage_write` içindeki `lba + count` toplamı uint32; çok
   büyük disklerde taşar ve şifreli bölge sınırı yanlış hesaplanır
   (`lib/storage.c:62`). Şifreli bölgeye düz veri sızabilir.
6. AHCI sürücüsünde tek bir ortak 64 KB DMA tamponu ve tek komut slotu var,
   kilit falan yok (`lib/blk_ahci.c:17`). Zamanlayıcı açıkken iki iş aynı anda
   diske dokunursa veriler karışır. ATA tarafında da benzeri var, blok
   önbelleği de korumasız.
7. `nvme_flush` boş bir fonksiyon (`lib/blk_nvme.c:164`). AFS günlüğü
   "flushladım" sandığı şeyin aslında disk cache'inden inmediğini bilmiyor;
   NVMe üzerinde çökmede journal eski bir durumu canlı verinin üstüne
   geri getirebilir.
8. Blok önbelleği flush'ında yazma hatası yok sayılıyor; disk yazamasız
   "temiz" görünüyor (`lib/blockdev.c:113`).
9. AFS'de dosya oluşturulurken LBA ayrılıyor ama `data_next` yalnız ilk
   yazmayla ilerliyor; hiç yazılmadan ikinci bir dosya oluşturulursa ikisi de
   aynı bloğu kullanır (`lib/afs.c`, `afs_create_handle` / `afs_write_handle`).
10. AFS dosya tutamakları ham slot numarası; dosya silinip yenisi oluşturulunca
    eldeki eski tutak yeni dosyaya bağlanır, kimse fark etmez (`lib/afs.c`).
11. Journal kayıtlarındaki `seq` numarası yazılıyor ama okunurken hiç
    karşılaştırılmıyor; eski dönemden kalmış bir günlük kaydı CRC geçerse
    olduğu gibi uygulanır (`lib/afs.c`, `journal_replay`).
12. Header CRC kontrolü `flags == 0` ise tamamen atlanıyor (`lib/afs.c:334`);
    bozuk bir tablo sessizce kabul edilip geri commit edilir.
13. AFS'te yeniden adlandırmada "isim çok uzun" hatası pratikte hiç dönmez,
    çünkü kopyalama zaten kırpıyor; uzun isimli iki dosya aynı kırpık ada
    çökebilir (`lib/afs.c:411` civarı).
14. DMA zaman aşımından sonra ATA sürücüsü reset atmadan PIO'ya düşüyor
    (`lib/blk_ata.c:165`); sürücü hâlâ önceki komutta olduğu için gelen
    çöp veri "okundu" sayılıyor.
15. Günlük bypass'lı büyük yazımlarda okuma tarafı aynı hatayı taşıyor:
    256 sektörden büyük okuma doğrudan diske gider, önbellekteki taze veri
    görünmez (`lib/blockdev.c:167`). Yazma tarafı düzeltildi (b17df5c),
    okuma tarafı duruyor.

## Güvenlik açıkları

Hobi sistem, ağa çıkıyor; bunlar "ileride" denmeyecek cinsten.

16. `tcp_send` / `tcp_receive` / `tcp_close` verilen `conn_id`'yi hiçbir
    şekilde doğrulamadan bağlantı dizisine indeksliyor (`kernel_plus/net/tcp.c:206`).
    Bu id uygulamaların ASI API'sinden doğrudan geliyor, yani herhangi bir
    uygulama çekirdek belleğinde gezinebilir.
17. `udp_send` 1500 baytlık yığın tamponu kullanıyor ama `ksock_send`
    65507'ye kadar kabul ediyor (`kernel_plus/net/udp.c:52`); üstüne
    `pseudo[12+total_len]` diye VLA var. Uygulama tarafindan çekirdek yığını
    taşırılabilir.
18. DNS ayrıştırıcısı sıkıştırma işaretçi zincirinde tek sıçramaya izin
    veriyor, sonra tekrar sıçrama görürse döngüden çıkmıyor
    (`kernel_plus/net/dns.c:73`); sinsi bir yanıt çekirdeği sonsuz döngüye sokar.
19. Gelen paketlerde IP/TCP/UDP sağlama toplamı hiç doğrulanmıyor, IP
    fragmentasyonu da kontrol edilmeden payload üst katmana veriliyor
    (`kernel_plus/net/ipv4.c:156`).
20. İzin diyaloğu açıkken (`s_policy_busy`) tüm izin soruları otomatik
    "ver"e dönüyor (`desktop/perm.c:583`); kullanıcı henüz butona basamadan
    uygulamanın timer'ları dosya sistemine ve ağa erişebiliyor.
21. `flags == 0` olan `.wlf` paketi CAP_ALL ile çalışıyor (`desktop/wlf.c:250`).
    Yetki türetme sadece flags doluysa yapılıyor; fail-closed olmalı.
22. Güncelleyicide imza, 8191 bayta kırpılmış kopya üzerinden doğrulanıyor,
    manifest ise tam parse ediliyor (`desktop/update.c:140`); ~8 KB sonrası
    imzayı bozmadan değiştirilebilir. Üstelik her şey HTTP'de.
23. `user_range` yalnızca sanal adres aralığına bakıyor, sayfanın haritalı
    olup olmadığına bakmıyor (`main_kernel/syscall.c:33`); kullanıcı programı
    çekirdeği kendi syscall'unda çökertebilir.
24. syscall fd tablosu tüm süreçler arasında tek (`main_kernel/syscall.c:11`);
    her uygulama diğerinin fd'sini kapatabilir. `SYS_GETPID` sabit 1,
    `SYS_TICKS` sabit 0 döner.
25. mbedTLS bağlantı katmanında `free()` boş bir fonksiyon, `realloc()` eski
    içeriği kopyalamıyor (`lib/mbedtls_port.c:184`). Her HTTPS bağlantısı
    kalıcı bellek sızdırıyor.
26. Kilit ekranı (`Ctrl+Alt+L`) sadece bir perde; masaüstündeki VM timer'ları,
    otosave ve güncelleme döngüleri arkada çalışmaya devam ediyor
    (`desktop/lock.c`).
27. Tarayıcı çerezleri süreç-küresel ve sayfalar arası temizlenmiyor
    (`desktop/apps/wolf_engine/main.c:665`); ayrıca https sayfalarının
    çerezleri http'de de gidiyor.
28. DHCP ACK doğrulanmıyor, DNS xid'i sıralı ve kaynak portu sabit
    (`kernel_plus/net/dhcp.c`, `dns.c`); yerel ağda taklit kolay.
29. wlf host'taki komut beyaz listesi ön ek eşleştirmesi yapıyor
    (`desktop/wlf_host.c:632`); `starts_with("ls")` kuralı `lsmeh` gibi
    komutlara da geçit veriyor.
30. şifre salt'ı saat değerinden üretiliyor (`desktop/auth.c:25`); hesap
    oluşturma anını bilen salt'ı hesaplar.

## Ağ yığını gerisi

31. TCP'de yeniden gönderim zamanlayıcısı tanımlı ama hiçbir yerden
    çağrılmıyor; FIN_WAIT slotları toplanmıyor; `ksock_close` CLOSE_WAIT
    dışındaki slotları sızdırıyor. Kayıp pakette bağlantı donar.
32. Karşı tarafın pencere büyüklüğü hiç okunmuyor (`tcp.c`), sınırsız
    gönderim yapılıyor.
33. `arp.c`'deki `arp_ticks` sayacı hiçbir yerde artırılmıyor; önbellek
    kayıtları hiç zaman aşımına uğramaz, router değişse de eski MAC kullanılır.
34. `ipv4_poll` içindeyken tekrar çağrılmasını engelleyen koruma, poll
    içinden tetiklenen ARP çözümlemesini imkansız kılıyor (`kernel_plus/net/ipv4.c:188`).
35. mbedTLS'in realloc kullanımı 25. maddedeki yüzünden el sıkışma
    tamponlarını bozabiliyor; TLS'de WANT_WRITE livelock yolu var.

## Çökme ve bellek (kabuk ve uygulamalar)

36. `.wlf` VM'leri kodu ortak bir tamponda (`s_wlf_buf`) pointer ile tutuyor;
    ikinci bir tara/kur/çalıştır o anda çalışan her uygulamanın kodunu
    bozar (`desktop/wlf.c:56`). Ayarlar'daki "Yeniden tara" tetikler.
37. wlf host tek küresel oturumla çalışıyor (`desktop/wlf_host.c:1627`);
    ikinci bir .wlf penceresi açınca ilk uygulamanın widget slotları
    karışır, tıklamalar `wid` çakışması yüzünden yanlış uygulamaya gider.
38. Pencere kayıtları (`winrec`) kapanışta serbest bırakılmıyor, 8 pencere
    dolununca 9.su ilk kaydın üstüne yazıyor (`desktop/windows.c:96`).
39. Ölçek/tema değişimi sonrası (`shell_rebuild`): duvar kağıdı imleci
    (`desktop/wallpaper.c:217`), tur kök nesnesi (`desktop/tour.c:23`),
    yükleme overlay timer'ı (`desktop/launch.c:79`) ve toast zamanlayıcısı
    (`desktop/shell_api.c:115`) ya bayat pointer taşıyor ya da hiç çizmiyor.
    Tour bir daha açılmaz ya da Esc'te çöker.
40. Ayarlar penceresi güncelleme denetimi sürerken kapatılırsa
    `g_upd_timer` silinmiş widget'lara yazıyor (`desktop/apps/settings/main.c:1222`).
41. `settings_set_int` bazı ayarlarda widget olayının ortasında senkron
    `shell_rebuild()` çağırıyor, yani kendi üstüne bağlı widget ağacı
    callback sırasında yok oluyor (`desktop/settings.c:124`).
42. İkinci bir onay diyaloğu açılırsa ilki yetim kalır ve ilk soru,
    ikinci diyaloğun cevabıyla sonuçlanır (`desktop/consent.c:180`).
43. Mağazadan uygulama silinince dizi kayıyor ama pencereler eski indeksi
    taşıyor; kapanan pencere yanlış uygulamanın "açık" bayrağını temizliyor
    (`desktop/store.c:46`).
44. Kalem kalem: görev çubuğu pil canvas tamponu her rebuild'te sızıyor
    (`taskbar.c:381`); DuckStore ikon önbelleği dolunca render tamponu
    sızıyor (`duckstore/main.c:245`); medya oynatıcı `fs->stat` boş kontrolü
    yok (`medya/main.c:155`); WolfEngine `lv_group`'ları kapatılırken
    silinmiyor (`asistan/main.c:190`).
45. Ağ çağrıları UI olay bağlamında senkron: Ayarlar'daki "Otomatik
    yapılandır" DHCP boyunca masaüstünü komple donduruyor
    (`desktop/apps/settings/main.c:329`), güncelleme indirmesi ve 6 MB'lık
    ekran görüntüsü yazımı da aynı şekilde.
46. Tarayıcıda JS köprüsü DOM döngüsü oluşturabiliyor (`appendChild`
    denetimi zayıf, `browser/main.c:5098`), iç içe `@media` ayrıştırması
    derinlik sınırısız; ikisi de kötü niyetli sayfada çökme demek.
47. lexbor entegrasyonundaki ağaç yürüyüşü derin sayfada (`~65k iç içe div`)
    uygulama yığınını taşırıyor (`desktop/apps/lexbor/main.c:306`).
48. `lib/png.c` chunk uzunluğunda uint32 taşmasına yenik düşüyor
    (`png.c:302`); özenle bozulmuş PNG yığın taşırır. `lib/svg.c`'de
    `1e2000000000` gibi bir üssü 2 milyar turluk döngüye çeviren sayı
    ayrıştırıcısı var (`svg.c:106`).
49. `lib/alloc.c` çok büyük boyut isteklerinde `(size+15)&~15` taşmasıyla
    kapasite denetimini atlatıp küçük blok döndürebilir (`alloc.c:25`).

## Sürücüler ve çekirdek

50. Exception sırasında recover-longjmp'u o anki IRQ'nun EOI'sini
    atlar (`main_kernel/isr.c:175`); ilgili hat ve slave PIC susar.
51. USB klavye kuyruğu yalnız PS/2 IRQ1 içinde boşaltılıyor ve modifier
    baytlarında döngü `return` ile kesiliyor (`kernel_plus/drivers/keyboard.c:306`);
    PS/2'siz makinede tuşlar kaybolur.
52. Klavye komut byte okuması zaman aşımına düşerse config=0 geri yazılır,
    IRQ1 ve çeviri bitleri ölür (`keyboard.c:515`).
53. FADT kısa olan makinelerde ACPI ayrıştırıcısı tablo dışına okuyor;
    `acpi_enter_sleep` hiçbir yerden çağrılmıyor, uyku/idle S3'ü fiilen yok
    (`kernel_plus/drivers/acpi.c`).
54. NVMe komutu zaman aşımında yeniden kuyrukluyor ama önceki denemenin
    kapısı çalınmış olabiliyor; gerçek donanımda SQ/CQ kayması riski
    (`lib/blk_nvme.c:81`).
55. `backbuffer_blit_rect` kırpması `x+w` taşmasıyla aşılabiliyor;
    `backbuffer_resize` LFB boyutuna göre kenetlemiyor
    (`kernel_plus/drivers/backbuffer.c`).
56. `clock_sleep_ms` kesmeler kapalıysa sonsuza kadar bekler; sürücü
    init yollarında çağrılıyor (`kernel_plus/system/clock.c:40`).
57. IDE diskin kimlik bilgisinde LBA48 kelimeleri hiç okunmuyor; 128 GB
    üstü IDE disk sessizce kırpılır (`lib/blk_ata.c:200`).
58. MMIO eşleştirmeleri PCD/WTT bayraksız, yani cache'lenebilir; NVMe
    kapı zillerinde gerçek donanımda sorun çıkarabilir (`lib/blockdev.c:232`).
59. `in_key_avail` init tarafında hep 0 dönen stub; `SYS_READ`'de dosya
    ofseti hiç yok, ring3'te sonsuz okuma döngüsü EOF göremez.

## Dosya sistemi (AFS/FAT/ISO)

60. AFS izin modeli ters çalışıyor: silme/yeniden adlandırma hedef
    dosyanın W bitine bakıyor, içindeki dizine değil; dosya oluştururken
    ebeveyn dizin izni hiç sorgulanmıyor (`lib/afs.c`, `perm_ok` çağrıları).
61. FAT zincir takibi döngü/küme limiti denetlemiyor (`lib/fat32.c:138`);
    bozuk USB diski takıp `D:`e girmek sistemi asılı bırakır, çünkü volfs
    ilk erişimde otomatik bağlar. FAT12, FAT32 sanılarak yanlış okunur;
    I/O hatası "zincir burada bitiyor" diye yutulur.
62. ISO9660 dizin girdisinde ad uzunluğu kayıt uzunluğuna karşı
    denetlenmiyor, tampon sonrasından okuyabilir (`lib/iso9660.c:80`).
63. AFS kapasite alanı uint32; 2 TB üstü disklerde hem taşma hem
    erişilemeyen alan riski var (`lib/afs.c:133`).

## Çalışıyor gibi görünen ama çalışmayan

64. Ayarlardaki "Çalışma alanı" sayacı hiçbir şeye bağlı değil; görev
    çubuğundaki nokta çizimi hiç yapılmıyor (`desktop/taskbar.c:376`).
65. Başlat menüsündeki dişli (tüm uygulamalar) butonu AI butonunun
    altında kalıyor, fareyle basılamıyor (`desktop/startmenu.c:570`).
66. Masaüstü simgesini sağ tıkla sil, widget'ı silip `C:/icons/` altındaki
    dosyayı bırakıyor; rebuild'te simge geri gelir (`desktop/desktop_icons.c:318`).
67. Mağazadan kurulum sonrası kayıt taraması uzantısız yolu deniyor,
    başarısız oluyor; kurulu uygulamanın sürüm bilgisi eski kalır
    (`desktop/wlf.c:419`).
68. `update_apply` hiç kontrol çalışmamışsa bile sürümü "güncellendi"
    diye işaretleyip hiçbir şey kurmuyor (`desktop/update.c:327`).
69. `desktop/shell/desktop.c` ve `desktop/cursor.c` derlemeye hiç girmiyor
    (Makefile'da yoklar); içindeki legacy açılış/kapatma yolu ölü duruyor.
    Ya canlandırılmalı ya da silinmeli.
70. TCP yeniden gönderim, IP fragmentasyonu, gerçek S3 uyku, `SYS_GETPID`:
    var gibi duruyorlar, fiilen çalışmıyorlar.

## Küçük şeyler

71. `lib/printf.c`: format sonda `%` ile biterse NUL sonrasını okur;
    `%ld`/`%llu` va_arg tüketmiyor, sonraki argümanlar kayıyor.
72. `lib/serial.c` genişlik dolgusu 32 baytlık tamponu kırpma denetimsiz
    doldurabilir (şimdilik kimse öyle çağırmıyor).
73. `lib/svg.c` 8'den derin `<g>` iç içe geçince stack'i dengeli
    tutmuyor; alt-yol sınırını aşan şekiller sessizce birleşiyor.
74. Ayarlar uygulamasında sekmelerin son ikisi aktiflik vurgulamasına
    hiç girmiyor (döngü `i<7` ama 9 sekme var, `settings/main.c:173`).
75. Terminalde 175 karakterlik yolun altında 1 baytlık NUL taşması;
    boru hattı verisi dosyaya yazarken 20 KB, boruda 8 KB'a sessizce
    kırpılıyor (`terminal/main.c:1762`, `:2889`).
76. `sfw_num` port ayrıştırması 12 haneli girişte int taşamasına giriyor.
