# ANOMAL-01 // DECONSTRUCT CORE

Drumazon 2 + Acid V -> Cyberdrive -> I Heart NY zincirini **tek bir plugin** olarak barındıran JUCE projesi.
Ses, preset'ler ve pluginlerin kendi GUI'leri orijinal kalır; üstüne 8 macro'luk tek panel gelir.

## Ne yapar
- Ableton Live'a tek VST3/AU olarak yüklenir, tek pencere açar.
- Sinyal: `Drumazon 2 + Acid V  ->  Cyberdrive  ->  I Heart NY`
- **DECK** sekmesi: 8 macro, scope, çıkış metresi, plugin durumu.
- **DRUMAZON / ACID V / CYBERDRIVE / I HEART NY** sekmeleri: gerçek plugin arayüzleri aynı pencerede gömülü.
- Macro'ya sağ tık: hangi plugin'in hangi parametresine bağlanacağını seç (birden fazla hedef eklenebilir).
- Host transport'u pluginlere iletilir; Drumazon ve Acid V'nin dahili sequencer'ları Live'ın tempo/play'ine senkron çalışır.
- Tüm durum (macro haritası + her plugin'in kendi preset'i) Live set'iyle birlikte kaydedilir.
- MIDI yönlendirme (üstteki kutu): `Split` = kanal 10 Drumazon, diğerleri Acid V.

## Derleme (GitHub Actions, Xcode gerekmez)
1. Klasörü bir GitHub reposuna push et.
2. Actions sekmesinde `build-macos` çalışır (macos-14, arm64).
3. Çıkan `ANOMAL-01-macos-arm64` artifact'ini indir, zip'i aç.

## Kurulum (Mac)
```bash
cd ~/Downloads/ANOMAL-01-macos-arm64        # açılan klasör
xattr -dr com.apple.quarantine .
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
cp -R ANOMAL-01.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R ANOMAL-01.component ~/Library/Audio/Plug-Ins/Components/
```
Live'da Preferences > Plug-Ins > Rescan. Sonra MIDI track'e **ANOMAL-01**'i yükle.

## Yerel derleme (istersen)
```bash
brew install cmake
cmake -B build -DCMAKE_BUILD_TYPE=Release     # plugin'i ~/Library/Audio/Plug-Ins altına kopyalar
cmake --build build --config Release -j 8
```

## İlk çalıştırma
1. Plugin ilk açıldığında VST3 klasörlerini otomatik tarar (SCAN butonu ile tekrar edilebilir).
   Pluginlerin **arm64 yerel** ve **VST3** formatında kurulu olması gerekir.
2. Dört plugin da otomatik atanır (isimle eşleşir). Yüklenmeyen varsa DECK'teki log kırmızı gösterir.
3. Varsayılan macro eşlemeleri parametre **adı tahminine** dayanır. Eşleşmeyen macro "unmapped" görünür.
   `PARAMS` butonu tüm parametre adlarını `~/Library/Application Support/ANOMAL-01/parameters.txt`'ye döker;
   o dosyayı bana gönderirsen varsayılanları gerçek adlara göre düzeltirim.

## Bilinen sınırlar (v0.1)
- Taramada yalnızca VST3 kullanılır (AU taraması arka plan thread'inde güvenilmez).
- Macro -> plugin yönü tek taraflıdır: plugin GUI'sinde knob çevirirsen macro kaymaz.
- Barındırılan pluginlerin lisans/aktivasyonu normal şekilde makinede yapılmış olmalı.
- Derleme hatası çıkarsa CI log'unu paylaş, düzeltilir.
