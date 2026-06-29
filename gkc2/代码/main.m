%% main.m
% 工科创 II：课程项目2

clear; clc; close all;

%% 1. 参数设置（任务一数据）
params.dt = 0.25;                 % 时间步长，15 min = 0.25 h
params.steps_per_day = 96;        % 每天96个点
params.days = 365;                % 全年365天
params.N = params.steps_per_day * params.days;

% 电价（元/kWh）
params.c_peak = 0.677;
params.c_flat = 0.667;
params.c_valley = 0.337;
params.c_sell = 0.40;             % 余电上网电价

% 储能效率与SOC约束
params.eta_ch = 0.90;
params.eta_dis = 0.90;
params.soc_min = 0.10;
params.soc_max = 0.90;
params.soc_init = 0.50;

% ===== 新增：储能寿命与折损成本参数 =====
params.c_bat_cap = 1500;          % 磷酸铁锂电池单位容量造价估算 (元/kWh)
params.cycle_life = 6000;         % 电池标称循环寿命 (次)
% 计算度电折损成本 (元/kWh)：造价 / (单次循环总可用吞吐量)
params.c_wear = params.c_bat_cap / (params.cycle_life * 2 * (params.soc_max - params.soc_min));

% 任务一参数
params.pv_rated = 14;             % 光伏峰值容量 kWp
params.E_load_day_target = 16;    % 家庭基础日均用电量 kWh/day
params.E_ev_bat = 60;             % EV电池容量 kWh（本任务5不直接参与计算）
params.e_ev = 0.15;               % EV单位里程耗电 kWh/km
params.d_ev = 30;                 % EV日均通勤里程 km
params.ev_power = 7;              % 充电桩额定功率 kW
params.ev_start_hour = 19;        % EV开始充电时刻 h

% ===== 策略3：预测型日前调度参数 =====
params.s3_peak_start = 17;
params.s3_peak_end = 21;
params.s3_prepeak_start = 7;
params.s3_prepeak_end = 17;

params.s3_reserve_factor = 1.10;     % 预测不确定性安全系数
params.s3_soc_target_max = 0.85;     % 谷充最高目标SOC
params.s3_flat_hold_soc = 0.40;      % 白天平段尽量保留的SOC

%% 2. 生成全年数据（任务2/3/4结果作为任务5输入）
[data, params] = prepare_data(params);
%% 2.1 导出全年时序数据 CSV（365×96）

P_load_base_mat = reshape(data.P_load_base, params.steps_per_day, params.days)';
P_ev_mat        = reshape(data.P_ev,        params.steps_per_day, params.days)';
P_pv_mat        = reshape(data.P_pv,        params.steps_per_day, params.days)';

writematrix(P_load_base_mat, 'year_load_curve.csv');   % 年负荷曲线：基础负荷
writematrix(P_ev_mat,        'year_ev_curve.csv');     % 年EV负荷曲线
writematrix(P_pv_mat,        'year_pv_curve.csv');     % 年光伏曲线

fprintf('=== 全年输入数据检查 ===\n');
fprintf('基础负荷平均日用电量: %.2f kWh/day\n', params.E_load_base_day);
fprintf('EV平均日充电量: %.2f kWh/day\n', params.E_ev_day);
fprintf('总平均日用电量: %.2f kWh/day\n', params.E_load_total_day);
fprintf('全年光伏总发电量: %.2f kWh\n\n', params.E_pv_year);

%% 3. 任务五：储能方案矩阵扫描（策略1）
% 候选容量与功率
E_list = [5 10 15 20 30];         % kWh
P_list = [2 3 5 7 10];            % kW

strategy_id = 1;

% 预分配结果矩阵
scan_cost = zeros(length(E_list), length(P_list));          % 年总电费
scan_selfuse = zeros(length(E_list), length(P_list));       % 自发自用率
scan_buy = zeros(length(E_list), length(P_list));           % 年购电量
scan_sell = zeros(length(E_list), length(P_list));          % 年售电量
scan_peak_import = zeros(length(E_list), length(P_list));   % 最大购电功率
scan_eq_cycles = zeros(length(E_list), length(P_list));     % 年等效循环次数

% 用于生成结果表
n_case = length(E_list) * length(P_list);
records = zeros(n_case, 8);
rec_id = 0;

for i = 1:length(E_list)
    for j = 1:length(P_list)

        % 当前储能方案
        battery.E_rated = E_list(i);
        battery.P_rated = P_list(j);
        battery.soc_min = params.soc_min;
        battery.soc_max = params.soc_max;
        battery.soc_init = params.soc_init;

        % 年度仿真
        result_tmp = simulate_battery(data, battery, params, strategy_id);

        % 统计指标
        annual_cost = result_tmp.total_cost;
        annual_buy = result_tmp.E_buy;
        annual_sell = result_tmp.E_sell;
        self_use = result_tmp.self_use_ratio;
        peak_import = max(result_tmp.P_grid_import);
        eq_cycles = result_tmp.eq_cycles;

        % 存矩阵（用于画图）
        scan_cost(i,j) = annual_cost;
        scan_selfuse(i,j) = self_use;
        scan_buy(i,j) = annual_buy;
        scan_sell(i,j) = annual_sell;
        scan_peak_import(i,j) = peak_import;
        scan_eq_cycles(i,j) = eq_cycles;

        % 存表（用于排序和导出）
        rec_id = rec_id + 1;
        records(rec_id, :) = [ ...
            battery.E_rated, ...
            battery.P_rated, ...
            annual_cost, ...
            annual_buy, ...
            annual_sell, ...
            self_use, ...
            peak_import, ...
            eq_cycles];
    end
end

%% 4. 结果整理与最优方案选择
ResultTable = array2table(records, ...
    'VariableNames', { ...
    'E_bat_kWh', ...
    'P_bat_kW', ...
    'AnnualCost_Yuan', ...
    'AnnualBuy_kWh', ...
    'AnnualSell_kWh', ...
    'SelfUseRatio', ...
    'PeakGridImport_kW', ...
    'EqCycles'});

ResultTable = sortrows(ResultTable, ...
    {'AnnualCost_Yuan','SelfUseRatio','PeakGridImport_kW','E_bat_kWh','P_bat_kW'}, ...
    {'ascend','descend','ascend','ascend','ascend'});

BestScheme = ResultTable(1,:);

% 取最优方案再跑一遍，便于后续画图
battery_best.E_rated = BestScheme.E_bat_kWh;
battery_best.P_rated = BestScheme.P_bat_kW;
battery_best.soc_min = params.soc_min;
battery_best.soc_max = params.soc_max;
battery_best.soc_init = params.soc_init;

result_best = simulate_battery(data, battery_best, params, strategy_id);

%% 5. 输出结果
fprintf('=== 任务五：储能方案扫描结果（策略1）===\n');
fprintf('最优储能容量 = %.1f kWh\n', BestScheme.E_bat_kWh);
fprintf('最优储能额定功率 = %.1f kW\n', BestScheme.P_bat_kW);
fprintf('全年总电费(含折损) = %.2f 元\n', BestScheme.AnnualCost_Yuan);
fprintf('全年购电量 = %.2f kWh\n', BestScheme.AnnualBuy_kWh);
fprintf('全年售电量 = %.2f kWh\n', BestScheme.AnnualSell_kWh);
fprintf('光伏自发自用率 = %.2f %%\n', BestScheme.SelfUseRatio*100);
fprintf('家庭向电网购电峰值 = %.2f kW\n', BestScheme.PeakGridImport_kW);
fprintf('储能年等效循环次数 = %.2f\n\n', BestScheme.EqCycles);

ResultTable_CN = ResultTable;  % 建一个副本用于显示
ResultTable_CN.Properties.VariableNames = {...
    '储能容量_kWh',...
    '储能功率_kW',...
    '全年综合成本_元',...
    '全年购电量_kWh',...
    '全年售电量_kWh',...
    '自发自用率',...
    '购电峰值_kW',...
    '等效循环次数'};

fprintf('=== 电费最低的前5个方案 ===\n');
disp(ResultTable_CN(1:min(5,height(ResultTable_CN)),:));

%% 6. 导出 CSV
writetable(ResultTable, 'task5_scan_results.csv');

%% 7. 作图 
plot_results(data, result_best, params);

figure;
imagesc(P_list, E_list, scan_cost);
set(gca, 'YDir', 'normal'); colorbar;
xlabel('储能额定功率 P_{bat,rated} (kW)'); ylabel('储能额定容量 E_{bat} (kWh)');
title('不同储能方案的全年总电费（含折损）（元）');

figure;
imagesc(P_list, E_list, scan_selfuse * 100);
set(gca, 'YDir', 'normal'); colorbar;
xlabel('储能额定功率 P_{bat,rated} (kW)'); ylabel('储能额定容量 E_{bat} (kWh)');
title('不同储能方案的光伏自发自用率（%）');

figure;
imagesc(P_list, E_list, scan_peak_import);
set(gca, 'YDir', 'normal'); colorbar;
xlabel('储能额定功率 P_{bat,rated} (kW)'); ylabel('储能额定容量 E_{bat} (kWh)');
title('不同储能方案的家庭向电网购电峰值（kW）');

%% 8. 任务六：固定最优储能参数，比较不同策略
battery_fix = battery_best;   

strategy_list = [1 2 3];
n_strategy = length(strategy_list);

records6 = zeros(n_strategy, 7);
result_strategy = cell(n_strategy, 1);

for k = 1:n_strategy
    sid = strategy_list(k);
    result_strategy{k} = simulate_battery(data, battery_fix, params, sid);

    records6(k,:) = [ ...
        sid, ...
        result_strategy{k}.E_buy, ...
        result_strategy{k}.E_sell, ...
        result_strategy{k}.Cost_buy - result_strategy{k}.Revenue_sell, ... % 纯账面电费
        result_strategy{k}.Cost_wear, ...      % 电池纯折损费
        result_strategy{k}.total_cost, ...     % 综合总电费
        result_strategy{k}.eq_cycles ];
end

Task6Table = array2table(records6, ...
    'VariableNames', { ...
    'StrategyID', ...
    'AnnualBuy_kWh', ...
    'AnnualSell_kWh', ...
    'NetElectricBill_Yuan', ... % 纯电费
    'BatteryWearCost_Yuan', ... % 折损费
    'TotalComprehensiveCost_Yuan', ... % 综合成本
    'EqCycles'});
Task6Table_CN = Task6Table;   % 副本
Task6Table_CN.Properties.VariableNames = {...
    '策略编号',...
    '全年购电量_kWh',...
    '全年售电量_kWh',...
    '纯电费_元',...
    '电池折损费_元',...
    '综合总成本_元',...
    '等效循环次数'};

fprintf('=== 任务六：不同策略对比结果 ===\n');
disp(Task6Table_CN);
writetable(Task6Table, 'task6_strategy_compare.csv');