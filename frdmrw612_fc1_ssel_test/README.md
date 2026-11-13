# FRDM-RW612 Flexcomm1 SPI slave-select test

> **Demo，僅供參考，非正式產品。** 本程式用於示範做法，未經量產等級的驗證，也不提供技術支援或維護承諾。

測試問題：FRDM-RW612 的 Flexcomm1 SPI（SCK/MISO/MOSI 在 GPIO7/8/9）接兩個 slave 時，GPIO6（SSEL0）與 GPIO10（SSEL1）能否在**不修改 MCUXpresso SDK** 的情況下分開控制。

結論：可以。兩個 pinmux 巨集依序設定後，只要改 `sselNum` 就能選擇要動作的 SSEL。

## 做法

```c
/* Pinmux：順序不能反，FC1_SPI_SS0 會清掉 SS1 的選擇位元 */
IO_MUX_SetPinMux(IO_MUX_FC1_SPI_SS0);   /* GPIO6/7/8/9 */
IO_MUX_SetPinMux(IO_MUX_FC1_SPI_SS1);   /* 加上 GPIO10 */

/* 每次傳輸前選擇 SSEL（SDK 公開 API） */
((spi_config_t *)SPI_GetConfig(SPI1))->sselNum = kSPI_Ssel0;   /* 或 kSPI_Ssel1 */
xfer.configFlags = kSPI_FrameAssert;                           /* 傳完放開 SSEL */
SPI_MasterTransferBlocking(SPI1, &xfer);
```

## 測試程式

每 5 ms 送出 4 個 byte（`55 AA 0F F0`，1 MHz，mode 0）。在 serial console（115200 8N1）送任意按鍵切換到下一個模式：

| 模式 | Pinmux | SSEL | 預期波形 |
|---|---|---|---|
| 1 | 只設 `FC1_SPI_SS0` | SSEL0 | GPIO6 低脈衝，GPIO10 維持高 |
| 2 | 只設 `FC1_SPI_SS1` | SSEL1 | GPIO10 低脈衝，GPIO6 維持高 |
| 3 | `FC1_SPI_SS0` + `FC1_SPI_SS1` | SSEL0 | GPIO6 低脈衝，GPIO10 維持高 |
| 4 | `FC1_SPI_SS0` + `FC1_SPI_SS1` | SSEL1 | GPIO10 低脈衝，GPIO6 維持高 |

每次切換會印出模式說明與 `MCI_IO_MUX->FC1` 的值（`0x008` = SS0，`0x020` = SS1，`0x028` = 兩者）。

Serial Monitor 的 Line ending 若設為 None，空白訊息不會送出任何資料；請輸入一個字元再送出。

## 量測點

| 訊號 | 腳位 | 位置 |
|---|---|---|
| SCK | GPIO7 | J2 pin 12 |
| MOSI | GPIO9 | J1 pin 2 |
| SSEL0 | GPIO6 | J2 pin 6 |
| SSEL1 | GPIO10 | mikroBUS J6 CS |

不需要接任何 SPI 裝置。

## 實測結果

2026-11-13 在 FRDM-RW612 上以示波器觀察：模式 3 與模式 4 之間切換時，SSEL0 與 SSEL1 如預期分別動作。四個模式的暫存器值與上表一致。

測試條件只有 1 MHz、mode 0、未接實際裝置。

## 限制

- RW612 的 Flexcomm1 只有 SSEL0（GPIO6）與 SSEL1（GPIO10）接到腳位；第三個以上的 slave 需用一般 GPIO 當 CS。
- 速度、CPOL/CPHA、資料寬度是整個 Flexcomm 共用的，不同裝置需要不同設定時要在切換前重設。
- 切換 `sselNum` 必須在匯流排閒置時進行。

## 專案來源

由 MCUXpresso SDK 26.09.00 的 `hello_world`（FRDM-RW612）複製而來，SDK 本身沒有修改。與原始 hello_world 的差異：

| 檔案 | 差異 |
|---|---|
| `source/main.c` | 新增，取代 `hello_world.c` |
| `CMakeLists.txt` | 專案名稱與原始檔路徑 |
| `frdmrw612/prj.conf` | 加入 `driver.flexcomm_spi` |
| `frdmrw612/board_files.cmake` | 加入 `PRINTF_ADVANCED_ENABLE=1`（`%02X` 補零） |
| `.vscode/mcuxpresso-tools.json` | 專案名稱 |

## 建置

本 repo 不含 SDK。需要 MCUXpresso SDK 26.09.00（FRDM-RW612）與 Arm GNU Toolchain 14.2。

1. 把 `mcux_include.json.template` 複製成 `mcux_include.json`，將 `<TOOLS_DIR>`（通常是 `C:/Users/你的帳號/.mcuxpressotools`）與 `<SDK_ROOT>` 換成本機路徑。
2. 把 `.vscode/mcuxpresso-tools.json` 的 `sdk.path` 改成本機的 SDK 路徑。
3. `cmake --preset debug`，再 `cmake --build --preset debug`。`debug` 是 RAM 映像，由 debugger 載入執行。

## 授權

`source/main.c` 以 [BSD-3-Clause](../LICENSE) 授權，按現狀提供，不附任何擔保。NXP 的板級檔與 SVD 維持各自檔頭的授權（BSD-3-Clause），`FreeRTOSConfigBoard.h` 為 MIT。
