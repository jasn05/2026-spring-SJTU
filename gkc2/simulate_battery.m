function result = simulate_battery(data, battery, params, strategy_id)
% simulate_battery.m
% 计入电池折损成本与防亏本套利逻辑

N = length(data.P_load_total);
dt = params.dt;

E_max = battery.E_rated * battery.soc_max;
E_min = battery.E_rated * battery.soc_min;
E_init = battery.E_rated * battery.soc_init;
P_rated = battery.P_rated;

eta_ch = params.eta_ch;
eta_dis = params.eta_dis;

E_bat = zeros(N+1,1);
E_bat(1) = E_init;

P_bat_ch = zeros(N,1);
P_bat_dis = zeros(N,1);
P_grid_import = zeros(N,1);
P_grid_export = zeros(N,1);
P_pv_self = zeros(N,1);  

steps_per_day = params.steps_per_day;
valley_start_step = round(23 / dt) + 1;   

% ===== 核心优化：计算峰谷套利真实利润 =====
% 利润 = 峰段放1度电省下的钱 - 谷段充进这1度电花的钱 - 放这1度电的电池折损
arbitrage_profit = params.c_peak - (params.c_valley / (eta_ch * eta_dis)) - params.c_wear;

E_target_valley = calc_valley_target(1, data, battery, params, E_min, E_max, arbitrage_profit);

for t = 1:N
    P_load = data.P_load_total(t);
    P_pv = data.P_pv(t);
    price_t = data.price(t);

    day_id = ceil(t / steps_per_day);
    step_in_day = mod(t-1, steps_per_day) + 1;

    if t == 1
        E_target_valley = calc_valley_target(1, data, battery, params, E_min, E_max, arbitrage_profit);
    elseif step_in_day == valley_start_step
        plan_day = min(day_id + 1, params.days);
        E_target_valley = calc_valley_target(plan_day, data, battery, params, E_min, E_max, arbitrage_profit);
    end

    is_peak = abs(price_t - params.c_peak) < 1e-9;
    is_valley = abs(price_t - params.c_valley) < 1e-9;
    P_surplus = P_pv - P_load;

    pch = 0; pdis = 0; pbuy = 0; psell = 0;

    if strategy_id == 1
        if P_surplus >= 0
            P_ch_limit_energy = max(0, (E_max - E_bat(t)) / (eta_ch * dt));
            pch = min([P_surplus, P_rated, P_ch_limit_energy]);
            psell = P_surplus - pch;
            P_pv_self(t) = P_load + pch;
        else
            P_deficit = -P_surplus;
            P_dis_limit_energy = max(0, (E_bat(t) - E_min) * eta_dis / dt);
            pdis = min([P_deficit, P_rated, P_dis_limit_energy]);
            pbuy = P_deficit - pdis;
            P_pv_self(t) = P_pv;
        end

    elseif strategy_id == 2
        if P_surplus >= 0
            P_ch_limit_energy = max(0, (E_max - E_bat(t)) / (eta_ch * dt));
            pch = min([P_surplus, P_rated, P_ch_limit_energy]);
            psell = P_surplus - pch;
            P_pv_self(t) = P_load + pch;
        else
            P_deficit = -P_surplus;
            if is_peak || abs(price_t - params.c_flat) < 1e-9
                P_dis_limit_energy = max(0, (E_bat(t) - E_min) * eta_dis / dt);
                pdis = min([P_deficit, P_rated, P_dis_limit_energy]);
                pbuy = P_deficit - pdis;
            elseif is_valley
                pbuy = P_deficit;
                % 【优化点】：只有套利赚钱，才用市电充电。否则只保底防断电。
                if arbitrage_profit > 0 && E_bat(t) < 0.30 * battery.E_rated
                    P_ch_limit_energy = max(0, (E_max - E_bat(t)) / (eta_ch * dt));
                    pch = min([P_rated, P_ch_limit_energy]);
                    pbuy = pbuy + pch;
                end
            end
            P_pv_self(t) = P_pv;
        end

    elseif strategy_id == 3
        if P_surplus >= 0
            P_ch_limit_energy = max(0, (E_max - E_bat(t)) / (eta_ch * dt));
            pch = min([P_surplus, P_rated, P_ch_limit_energy]);
            psell = P_surplus - pch;
            P_pv_self(t) = P_load + pch;
        else
            P_deficit = -P_surplus;
            if is_valley
                pbuy = P_deficit;
                if E_bat(t) < E_target_valley
                    P_ch_limit_energy = max(0, (E_target_valley - E_bat(t)) / (eta_ch * dt));
                    pch = min([P_rated, P_ch_limit_energy]);
                    pbuy = pbuy + pch;
                end
            elseif is_peak
                P_dis_limit_energy = max(0, (E_bat(t) - E_min) * eta_dis / dt);
                pdis = min([P_deficit, P_rated, P_dis_limit_energy]);
                pbuy = P_deficit - pdis;
            else
                E_hold = max(E_min, params.s3_flat_hold_soc * battery.E_rated);
                if E_bat(t) > E_hold
                    P_dis_limit_energy = max(0, (E_bat(t) - E_hold) * eta_dis / dt);
                    pdis = min([P_deficit, P_rated, P_dis_limit_energy]);
                    pbuy = P_deficit - pdis;
                else
                    pbuy = P_deficit;
                end
            end
            P_pv_self(t) = P_pv;
        end
    else
        error('strategy_id 只能取 1, 2 或 3');
    end

    E_bat(t+1) = E_bat(t) + eta_ch * pch * dt - pdis * dt / eta_dis;
    E_bat(t+1) = min(max(E_bat(t+1), E_min), E_max);

    P_bat_ch(t) = pch;
    P_bat_dis(t) = pdis;
    P_grid_import(t) = pbuy;
    P_grid_export(t) = psell;
end

% === 年度统计 ===
E_buy = sum(P_grid_import) * dt;
E_sell = sum(P_grid_export) * dt;
Cost_buy = sum(P_grid_import .* data.price) * dt;
Revenue_sell = sum(P_grid_export) * dt * params.c_sell;

% 【优化点】：加入全年电池物理折损成本
Cost_wear = sum(P_bat_dis) * dt * params.c_wear;

% 修正后的总成本
total_cost = Cost_buy - Revenue_sell + Cost_wear;

E_pv_total = sum(data.P_pv) * dt;
E_pv_self = sum(P_pv_self) * dt;
self_use_ratio = E_pv_self / max(E_pv_total, 1e-9);

eq_cycles = (sum(P_bat_ch) * dt) / battery.E_rated / 2;

result.E_buy = E_buy;
result.E_sell = E_sell;
result.Cost_buy = Cost_buy;
result.Revenue_sell = Revenue_sell;
result.Cost_wear = Cost_wear;         % 导出折损费供分析
result.total_cost = total_cost;
result.self_use_ratio = self_use_ratio;
result.eq_cycles = eq_cycles;
result.E_bat = E_bat;
result.P_bat_ch = P_bat_ch;
result.P_bat_dis = P_bat_dis;
result.P_grid_import = P_grid_import;
result.P_grid_export = P_grid_export;
result.P_pv_self = P_pv_self;

end

function E_target = calc_valley_target(plan_day, data, battery, params, E_min, E_max, arbitrage_profit)
% 若套利本身是亏钱的，强行谷充峰放就没有意义，直接躺平（目标设为最低SOC）
if arbitrage_profit <= 0
    E_target = E_min;
    return;
end

steps_per_day = params.steps_per_day;
dt = params.dt;

idx1 = (plan_day - 1) * steps_per_day + 1;
idx2 = plan_day * steps_per_day;
P_load_day = data.P_load_total(idx1:idx2);
P_pv_day = data.P_pv(idx1:idx2);
hour = (0:steps_per_day-1) * dt;

mask_peak = (hour >= params.s3_peak_start) & (hour < params.s3_peak_end);
mask_prepeak = (hour >= params.s3_prepeak_start) & (hour < params.s3_prepeak_end);

E_peak_deficit = sum(max(P_load_day(mask_peak) - P_pv_day(mask_peak), 0)) * dt;
E_prepeak_surplus = sum(max(P_pv_day(mask_prepeak) - P_load_day(mask_prepeak), 0)) * dt;
E_need_from_valley = max(E_peak_deficit - E_prepeak_surplus, 0);

E_target = E_min + params.s3_reserve_factor * E_need_from_valley / params.eta_dis;
E_target = min(E_target, params.s3_soc_target_max * battery.E_rated);
E_target = min(max(E_target, E_min), E_max);

end