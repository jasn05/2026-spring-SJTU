function [data, params] = prepare_data(params)
% prepare_data.m
% 作用：
% 1) 生成全年家庭基础负荷（任务2）
% 2) 生成全年光伏出力曲线（任务3）
% 3) 生成全年EV充电负荷并叠加（任务4）
% 4) 同时生成分时电价曲线，便于后续任务5/6直接调用
%
% 输出：
% data.P_load_base   基础负荷 N×1
% data.P_pv          光伏功率 N×1
% data.P_ev          EV充电负荷 N×1
% data.P_load_total  总负荷 = 基础负荷 + EV N×1
% data.price         分时电价 N×1

N_day = params.steps_per_day;   % 96
dt = params.dt;                 % 0.25 h
days = params.days;             % 365

% ---------------------------
% 任务一参数接入（若main.m未给，则使用默认值）
% ---------------------------
if ~isfield(params, 'pv_rated')
    params.pv_rated = 14;       % kWp
end

if ~isfield(params, 'E_load_day_target')
    params.E_load_day_target = 16;   % kWh/day
end

if ~isfield(params, 'e_ev')
    params.e_ev = 0.15;         % kWh/km
end

if ~isfield(params, 'd_ev')
    params.d_ev = 30;           % km/day
end

if ~isfield(params, 'ev_power')
    params.ev_power = 7;        % kW
end

if ~isfield(params, 'ev_start_hour')
    params.ev_start_hour = 19;  % 19:00开始充电
end

% 分时电价，后续任务5/6会直接用
if ~isfield(params, 'c_peak')
    params.c_peak = 0.677;
end
if ~isfield(params, 'c_flat')
    params.c_flat = 0.667;
end
if ~isfield(params, 'c_valley')
    params.c_valley = 0.337;
end
if ~isfield(params, 'c_sell')
    params.c_sell = 0.40;
end

% EV日补能按任务一自动计算
params.ev_daily_energy = params.d_ev * params.e_ev;   % 4.5 kWh/day

% 预分配
P_load_base = zeros(days, N_day);
P_pv = zeros(days, N_day);
P_ev = zeros(days, N_day);
price = zeros(days, N_day);

% 月份天数
month_days = [31 28 31 30 31 30 31 31 30 31 30 31];
month_of_day = zeros(days,1);
idx0 = 1;
for m = 1:12
    month_of_day(idx0:idx0+month_days(m)-1) = m;
    idx0 = idx0 + month_days(m);
end

% 为了结果可复现
rng(2026);

for day = 1:days
    m = month_of_day(day);
    hour = (0:N_day-1) * dt;

    % =========================================
    % 任务2：生成全年家庭基础负荷
    % 要求体现：
    % - 工作日与周末差异
    % - 夏季空调抬升
    % - 冬季热水/采暖变化
    % - 晚间主高峰
    % =========================================

    % 夜间底座负荷（冰箱、待机、少量基础负荷）
    base = 0.18 * ones(1, N_day);

    % 早高峰（早餐、洗漱、热水等）
    morning_peak = 0.35 * exp(-((hour-7.5)/1.5).^2);

    % 中午小高峰（做饭/在家活动）
    noon_peak = 0.18 * exp(-((hour-12.5)/1.8).^2);

    % 晚间主高峰（照明、做饭、娱乐、电器集中使用）
    evening_peak = 0.95 * exp(-((hour-20.0)/2.8).^2);

    % 季节修正
    % 夏季：空调负荷明显上升
    if ismember(m, [6 7 8])
        seasonal_load = ...
            0.60 * exp(-((hour-14.5)/4.0).^2) + ...   % 午后空调
            0.45 * exp(-((hour-22.0)/2.2).^2);        % 夜间空调
    % 冬季：热水/采暖早晚增加
    elseif ismember(m, [12 1 2])
        seasonal_load = ...
            0.30 * exp(-((hour-6.5)/1.6).^2) + ...    % 早晨热水/采暖
            0.40 * exp(-((hour-21.0)/2.4).^2);        % 晚间热水/采暖
    % 春秋季：负荷较平缓
    else
        seasonal_load = ...
            0.10 * exp(-((hour-13.0)/3.0).^2) + ...
            0.15 * exp(-((hour-20.5)/2.8).^2);
    end

    % 工作日/周末差异
    % 这里约定 day=1 对应周一
    weekday_id = mod(day-1, 7) + 1;   % 1~7
    if weekday_id >= 6
        % 周末白天居家时间更长
        weekend_midday = 0.10 * exp(-((hour-15.0)/3.0).^2);
        day_factor = 1.08;
    else
        weekend_midday = 0;
        day_factor = 1.00;
    end

    P_load_base(day,:) = day_factor * ...
        (base + morning_peak + noon_peak + evening_peak + seasonal_load + weekend_midday);

    % =========================================
    % 任务3：生成全年光伏出力
    % 要求体现：
    % - 夜间为零
    % - 中午附近达到峰值
    % - 夏季更长更高，冬季更短更低
    % - 天气随机波动
    % =========================================

    sunrise = [7.0 6.7 6.3 5.8 5.3 5.0 5.1 5.4 5.8 6.2 6.6 6.9];
    sunset  = [17.2 17.8 18.3 18.8 19.1 19.3 19.2 18.8 18.1 17.5 17.0 16.8];
    pv_scale = [0.65 0.72 0.82 0.92 1.00 1.05 1.05 1.00 0.90 0.80 0.72 0.65];

    day_len = sunset(m) - sunrise(m);
    pv_shape = zeros(1, N_day);

    for k = 1:N_day
        if hour(k) >= sunrise(m) && hour(k) <= sunset(m)
            x = (hour(k) - sunrise(m)) / day_len;
            pv_shape(k) = sin(pi * x);   % 简化钟形曲线
        end
    end

    % 天气类型扰动：晴/多云/阴雨
    r = rand();
    if r < 0.60
        weather_factor = 0.95 + 0.10*rand();   % 晴天
    elseif r < 0.90
        weather_factor = 0.55 + 0.25*rand();   % 多云
    else
        weather_factor = 0.15 + 0.25*rand();   % 阴雨
    end

    P_pv(day,:) = params.pv_rated * pv_scale(m) * weather_factor .* pv_shape;

    % =========================================
    % 任务4：生成全年EV充电负荷
    % 假设：
    % - 每天回家后开始充电
    % - 充电功率受家用充电桩限制
    % - 每日补能需求 = d_ev * e_ev = 4.5 kWh/day
    % =========================================

    start_idx = round(params.ev_start_hour / dt) + 1;
    charge_steps = ceil(params.ev_daily_energy / (params.ev_power * dt));
    end_idx = min(N_day, start_idx + charge_steps - 1);

    P_ev(day, start_idx:end_idx) = params.ev_power;

    % 最后一个时段按精确能量修正
    actual_energy = sum(P_ev(day,:)) * dt;
    extra = actual_energy - params.ev_daily_energy;
    if extra > 0
        P_ev(day, end_idx) = max(0, P_ev(day, end_idx) - extra/dt);
    end

    % =========================================
    % 电价时序（给任务5/6预留）
    % =========================================
    for k = 1:N_day
        h = hour(k);
        if h >= 23 || h < 7
            price(day,k) = params.c_valley;
        elseif h >= 17 && h < 21
            price(day,k) = params.c_peak;
        else
            price(day,k) = params.c_flat;
        end
    end
end

% =========================================
% 将基础负荷整体校准到任务一给出的 16 kWh/day
% =========================================
E_load_base_year_raw = sum(P_load_base(:)) * dt;
E_load_base_day_raw = E_load_base_year_raw / days;

scale_load = params.E_load_day_target / E_load_base_day_raw;
P_load_base = P_load_base * scale_load;

% =========================================
% 转为 N×1 向量，供后续仿真使用
% =========================================
P_load_base = reshape(P_load_base', [], 1);
P_pv = reshape(P_pv', [], 1);
P_ev = reshape(P_ev', [], 1);
price = reshape(price', [], 1);

P_load_total = P_load_base + P_ev;

% 保存输出
data.P_load_base = P_load_base;
data.P_ev = P_ev;
data.P_load_total = P_load_total;
data.P_pv = P_pv;
data.price = price;

% 统计量，便于检查
params.scale_load = scale_load;
params.E_load_base_year = sum(P_load_base) * dt;
params.E_load_base_day = params.E_load_base_year / days;

params.E_ev_year = sum(P_ev) * dt;
params.E_ev_day = params.E_ev_year / days;

params.E_load_total_year = sum(P_load_total) * dt;
params.E_load_total_day = params.E_load_total_year / days;

params.E_pv_year = sum(P_pv) * dt;
params.E_pv_day = params.E_pv_year / days;

end