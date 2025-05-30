# 🚀 Welcome to EoH | E-Ra IoT Platform | AI Project Collection

Chào mừng bạn đến với kho tài nguyên **miễn phí** về các dự án liên quan đến **IoT**, **Embedded Systems**, và **AI**.  
Tất cả được chia sẻ **phi thương mại** nhằm hỗ trợ sinh viên, kỹ sư và cộng đồng đam mê công nghệ!
#### Nếu bạn đã cài Platform IO thì chỉ cần Download về => Vô VScode => File => Open Folder (Chọn folder source code bạn mới tải)=> Đợi khoảng 45 giây để Vscode Install thư viện cần thiết và các packages khác.
#### Nếu chưa bạn cài Platform IO : Bạn tham khảo video sau : 👉 [YouTube Channel](https://www.youtube.com/watch?v=FuLRXgD9C2s)
> 🔧 Dự án được xây dựng và duy trì bởi [**EoH JSC 🇻🇳**](https://e-ra.io/index.html)
---

## 📦 Phần cứng cần có

| Thiết bị                 | Mô tả                              |
| ------------------------ | ---------------------------------- |
| ESP32 Dev Module 30 pins | Vi điều khiển chính                |
| Cảm biến vân tay AS608   | Adafruit Fingerprint Sensor (UART) |
| LCD 16x2 I2C             | Màn hình hiển thị                  |
| Relay Module 5V          | Điều khiển mở cửa                  |
| Buzzer                   | Cảnh báo âm thanh                  |
| Nguồn 5V                 | Cấp nguồn cho hệ thống             |

---

## 📚 Cài thư viện (Arduino IDE hoặc PlatformIO)

### 👉 Arduino IDE

Cài qua **Library Manager**:

- `ERa`
- `Adafruit Fingerprint Sensor`
- `LiquidCrystal_I2C` (by johnrickman)

### 👉 PlatformIO (tuỳ chọn)

Thêm vào `platformio.ini`:

```ini
lib_deps =
    eoh-ltd/ERa
    adafruit/Adafruit Fingerprint Sensor Library
    johnrickman/LiquidCrystal_I2C
```

---

## 🔧 Cấu hình cần sửa trong mã nguồn

Trong file `main.cpp`, thay thế các dòng sau bằng thông tin của bạn:

```cpp
const char ssid[] = "your_SSID";
const char pass[] = "your_PASSWORD";

#define ERA_AUTH_TOKEN "_your_ERa_token_"

const char *GOOGLE_SCRIPT_PATH = "/macros/s/AKfycb.../exec";
```

---

## ☁️ Code cấu hình Google Apps Script

Tạo file mới tại [script.google.com](https://script.google.com), dán đoạn code sau và triển khai nó dưới dạng "Web App":

```javascript
/**
 * Hàm xử lý POST request từ ESP32
 */
function doPost(e) {
  try {
    // 1. Parse payload JSON
    var data = JSON.parse(e.postData.contents);
    var id = data.id;
    var time = data.time;
    var date = data.date;

    // 2. Mở sheet đầu tiên trong workbook
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];

    // 3. Tạo timestamp tại server (optional)
    var timestamp = new Date();

    // 4. Ghi một dòng mới
    sheet.appendRow([timestamp, id, time, date]);

    // 5. Trả về thành công
    return ContentService.createTextOutput(
      JSON.stringify({ status: "success" })
    ).setMimeType(ContentService.MimeType.JSON);
  } catch (error) {
    // Nếu có lỗi, trả về lỗi
    return ContentService.createTextOutput(
      JSON.stringify({ status: "error", message: error })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}
```

> ⚠️ Khi deploy, chọn **"Anyone"** để thiết bị có thể gửi dữ liệu tới script.

---

## 🎓 Tutorials Miễn Phí

Học hoàn toàn **miễn phí** với các video hướng dẫn thực hành:  
📺 [YouTube Channel: EoH - E-Ra IoT Platform](https://www.youtube.com/@eohchannelofficial)

---

## ❤️ Cảm ơn bạn!

> Nếu thấy hữu ích, hãy **Star ⭐** repo này và **Subscribe 🔔** kênh YouTube để ủng hộ EoH nhé!
