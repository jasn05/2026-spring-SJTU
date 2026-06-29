clc;
clear;
close all;

N = 160;

wc1 = 0.45;
wc2 = 0.65;

b = fir1(N,[wc1 wc2],'stop',hamming(N+1));

%% impulse response

figure;
stem(0:N,b);

title('Impulse Response h(n)');
xlabel('n');
ylabel('h(n)');

%% frequency response

[H,w] = freqz(b,1,1024);

figure;

plot(w/pi,abs(H),'LineWidth',1.5);

grid on;

title('Magnitude Response');

xlabel('\omega/\pi');

ylabel('|H(e^{j\omega})|');