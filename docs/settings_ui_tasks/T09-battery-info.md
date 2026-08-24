# T09：Battery/Info/BQ27220

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test9`

## 目标

接入真实电池信息和 BQ27220 校准操作，替换 Info 页面中的静态或缺失数据。

## 负责文件

- `settings_battery_calibration_page.hpp`
- 新增 Info/Battery 页面或 adapter。
- 新增电池解析/格式化测试。

## API

- `cp0_battery_read()` 或 `cp0_signal_bq27220_api({"Read"})`。
- `cp0_signal_bq27220_api({"Calibrate", "0".."3"})`。

校准序号：0 Enter CAL，1 CC Offset，2 Board Offset，3 Exit CAL。

## 工作内容

1. 复用旧 `Info`、`setup_info_model` 的字段格式化和有效性判断。
2. 显示电压、电流、温度、SOC、剩余容量等真实数据。
3. 读取失败时不能把旧数据当成当前有效数据。
4. 校准操作必须 pending、防重复、显示结果。
5. 页面销毁时确保 callback 不再访问页面。

## 验收

- 电池信息周期刷新正常。
- invalid battery 数据有错误状态。
- 四个校准动作均发送正确命令。
- 校准成功、失败、超时、退出中断均安全。
