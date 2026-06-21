工科创 II 课程项目提交说明
小组：第四小组
成员：姜圣基 · 赵子涵 · 林子源 · 李宇扬 · 吴宇豪
===========================

本文件用于说明本项目除“项目报告”以外的提交内容，包括数据文件、MATLAB 代码和运行方法。


一、提交内容总览

按照课程提交要求，最终应包含以下四类材料：

1. 项目报告
 - 本目录已包含项目报告。

2. 数据文件
   - 本目录已包含年度负荷、年度光伏、年度 EV 负荷和仿真结果 CSV 文件。

3. MATLAB 代码
   - 本目录已包含数据读取与预处理、负荷/PV/EV 曲线生成、储能仿真主程序和结果可视化程序。

4. 运行说明
   - 见本文档第五部分。


二、数据文件说明

1. year_load_curve.csv
   - 年负荷曲线 CSV。
   - 由 main.m 调用 prepare_data.m 生成。
   - 数据为 365 天、每天 96 个 15 min 时刻点。
   - 每一行对应一天，每一列对应一天内一个 15 min 时刻点。

2. year_pv_curve.csv
   - 年光伏出力曲线 CSV。
   - 由 main.m 调用 prepare_data.m 生成。
   - 数据为 365 天、每天 96 个 15 min 时刻点。

3. year_ev_curve.csv
   - 年 EV 充电负荷 CSV。
   - 由 main.m 调用 prepare_data.m 生成。
   - 数据为 365 天、每天 96 个 15 min 时刻点。

4. task5_scan_results.csv
   - 任务 5 储能容量和功率组合扫描结果。
   - 包含储能容量、储能功率、全年综合成本、购电量、售电量、光伏自发自用率、购电峰值和等效循环次数等指标。

5. task6_strategy_compare.csv
   - 任务 6 不同储能运行策略对比结果。
   - 包含策略编号、全年购电量、全年售电量、净电费、电池折损费、综合总成本和等效循环次数等指标。


三、MATLAB 代码说明

1. main.m
   - 项目主程序。
   - 完成参数设置、全年数据生成、储能方案扫描、最优方案选择、策略对比、CSV 导出和结果绘图。
   - 运行本文件即可复现实验数据和主要结果。

2. prepare_data.m
   - 数据读取与预处理 / 曲线生成程序。
   - 生成家庭基础负荷曲线、光伏出力曲线、EV 充电负荷曲线和分时电价曲线。
   - 输出结构体 data 和更新后的参数结构体 params。

3. simulate_battery.m
   - 储能仿真主函数。
   - 根据输入的负荷、光伏、EV、电价、储能容量和运行策略，逐时段计算储能充放电、电网购售电、成本和统计指标。
   - 支持不同策略编号，用于任务 5 和任务 6 的对比分析。

4. plot_results.m
   - 结果可视化程序。
   - 绘制典型日负荷/PV/EV 曲线、电网与储能功率曲线、全年储能电量变化曲线和月光伏发电量柱状图。


四、任务对应关系

1. 任务 1：基础参数设置
   - 对应 main.m 中的参数设置部分。

2. 任务 2：家庭基础负荷曲线生成
   - 对应 prepare_data.m。
   - 输出 year_load_curve.csv。

3. 任务 3：光伏出力曲线生成
   - 对应 prepare_data.m。
   - 输出 year_pv_curve.csv。

4. 任务 4：EV 充电负荷曲线生成
   - 对应 prepare_data.m。
   - 输出 year_ev_curve.csv。

5. 任务 5：储能容量和功率方案扫描
   - 对应 main.m 和 simulate_battery.m。
   - 输出 task5_scan_results.csv。

6. 任务 6：不同储能策略对比
   - 对应 main.m 和 simulate_battery.m。
   - 输出 task6_strategy_compare.csv。

7. 任务 7：结果可视化与分析支撑
   - 对应 plot_results.m 以及 main.m 中的热力图绘制部分。


五、运行方法

1. 打开 MATLAB。
2. 将 MATLAB 当前工作目录切换到本项目文件夹。
3. 在命令行窗口运行：

   main

4. 程序运行后会自动：
   - 生成 year_load_curve.csv；
   - 生成 year_pv_curve.csv；
   - 生成 year_ev_curve.csv；
   - 生成 task5_scan_results.csv；
   - 生成 task6_strategy_compare.csv；
   - 输出关键计算结果；
   - 弹出结果图形窗口。


