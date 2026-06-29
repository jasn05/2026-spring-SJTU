function plot_results(data, result, params)
% plot_results.m
% 绘制典型日曲线和电池能量曲线

steps_per_day = params.steps_per_day;
dt = params.dt;

% 取某一天作为示例，这里取第180天
show_day = 180;
idx1 = (show_day-1)*steps_per_day + 1;
idx2 = show_day*steps_per_day;
t = (0:steps_per_day-1) * dt;

figure;
plot(t, data.P_load_base(idx1:idx2), 'LineWidth', 1.2); hold on;
plot(t, data.P_ev(idx1:idx2), 'LineWidth', 1.2);
plot(t, data.P_load_total(idx1:idx2), 'LineWidth', 1.5);
plot(t, data.P_pv(idx1:idx2), 'LineWidth', 1.5);
legend('基础负荷', 'EV负荷', '总负荷', '光伏功率', 'Location', 'best');
xlabel('时刻 / h');
ylabel('功率 / kW');
title(sprintf('第 %d 天典型日曲线', show_day));
grid on;

figure;
plot(t, result.P_grid_import(idx1:idx2), 'LineWidth', 1.2); hold on;
plot(t, result.P_grid_export(idx1:idx2), 'LineWidth', 1.2);
plot(t, result.P_bat_ch(idx1:idx2), 'LineWidth', 1.2);
plot(t, result.P_bat_dis(idx1:idx2), 'LineWidth', 1.2);
legend('购电功率', '上网功率', '储能充电功率', '储能放电功率', 'Location', 'best');
xlabel('时刻 / h');
ylabel('功率 / kW');
title(sprintf('第 %d 天电网与储能功率', show_day));
grid on;

figure;
plot((0:length(result.E_bat)-1)*dt, result.E_bat, 'LineWidth', 1.2);
xlabel('全年时间 / h');
ylabel('储能电量 / kWh');
title('全年储能电量变化');
grid on;

% 月发电量柱状图（用光伏示例）
month_days = [31 28 31 30 31 30 31 31 30 31 30 31];
monthly_pv = zeros(12,1);
start_idx = 1;
for m = 1:12
    nstep = month_days(m) * steps_per_day;
    monthly_pv(m) = sum(data.P_pv(start_idx:start_idx+nstep-1)) * dt;
    start_idx = start_idx + nstep;
end

figure;
bar(monthly_pv);
xlabel('月份');
ylabel('月光伏发电量 / kWh');
title('月光伏发电量柱状图');
grid on;

end
