import py_serialimg
import numpy as np
import cv2

# --- BAŞLANGIÇ AYARI ---
# GÜNCELLEME 1 (ZORUNLU): DOĞRU COM PORTUNU GİRİN
# Aygıt Yöneticisi'ni (Device Manager) açın ve
# STM32 Nucleo-F446RE kartınızın portunu ("COMX") buraya yazın.
# Bu, F746G'den farklı bir port olacaktır!
COM_PORT = "COM7"  # !!! BURAYI NUCLEO F446RE'YE GÖRE GÜNCELLEYİN !!!

# GÜNCELLEME 2 (BİLGİLENDİRME): TEST GÖRÜNTÜSÜ
# Bir önceki 'py_serialimg.py' kütüphanenizin 'SERIAL_IMG_Write'
# fonksiyonu akıllıca yazılmış. Görüntüyü STM32'nin istediği
# boyuta (128x128) OTOMATİK olarak küçültecektir.
#
# BU YÜZDEN BU DOSYA ADINI DEĞİŞTİRMENİZE VEYA
# MANUEL 128x128'LİK BİR DOSYA OLUŞTURMANIZA GEREK YOK.
# Orijinal 'mandrill.tif' dosyanızı kullanmaya devam edebilirsiniz.
# TEST_IMAGE_FILENAME = "mandrill.tiff"
#TEST_IMAGE_FILENAME = "mandrill.png"
TEST_IMAGE_FILENAME = "mandrillBinary.png"
# --- AYAR SONU ---

print(f"Seri port {COM_PORT} başlatılıyor...")
py_serialimg.SERIAL_Init(COM_PORT)
print("Port başlatıldı.")

mandrill = TEST_IMAGE_FILENAME

try:
    while 1:
        print("\nSTM32'den istek bekleniyor (PollForRequest)...")
        # Bu noktada F446RE kartınız height=128, width=128 bilgilerini gönderecek
        rqType, height, width, format = py_serialimg.SERIAL_IMG_PollForRequest()

        # SENARYO 1: STM32 bir görüntü göndermek istiyor (MCU_WRITES)
        if rqType == py_serialimg.MCU_WRITES:
            print(f"STM32 görüntü gönderiyor (Boyut: {width}x{height}). Alınıyor...")
            # Kütüphane 128x128x2 byte veriyi okuyacak
            img = py_serialimg.SERIAL_IMG_Read()
            print("Görüntü başarıyla alındı.")

            # GÜNCELLEME 3 (İsteğe bağlı): Kayıt dosyasının adını netleştirdik
            received_filename = "OUTPUT.png"
            cv2.imwrite(received_filename, img)
            print(f"Görüntü '{received_filename}' olarak kaydedildi.")

        # SENARYO 2: STM32 bir görüntü almak istiyor (MCU_READS)
        elif rqType == py_serialimg.MCU_READS:
            print(f"STM32 görüntü istiyor (Boyut: {width}x{height}). Gönderiliyor...")
            # 'SERIAL_IMG_Write' fonksiyonu 'mandrill.tif' dosyasını
            # otomatik olarak {width}x{height} (128x128) boyutuna küçültecek.
            img = py_serialimg.SERIAL_IMG_Write(mandrill)
            print(f"'{mandrill}' görüntüsü (128x128'e küçültülerek) başarıyla gönderildi.")

except KeyboardInterrupt:
    print("\nProgram durduruldu.")
except Exception as e:
    print(f"\nBir hata oluştu: {e}")
    print(f"COM portunun ('{COM_PORT}') doğru olduğundan ve STM32 kartının bağlı olduğundan emin olun.")