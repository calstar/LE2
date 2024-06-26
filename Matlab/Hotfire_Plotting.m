%% LOAD HOTFIRE DATA

%Import Hotfire Dataset
Filename = ["C:\Users\liams\Downloads\hotfire2_2023-04-16 (cleaned).xlsx"];
Data = readtable(Filename);

% Load Variables
prst = 6100; %process start cell
burnst = 6315; %burn start
burnfin = 6360; %burn finish
prfin = 6450; %process finish
Time = table2array(Data(:,3)); %millisec
PT_O1 = table2array(Data(:,4)); %psi
PT_E1 = table2array(Data(:,6)); %psi
PT_C1 = table2array(Data(:,8)); %psi
LC1 = table2array(Data(:,9));
LC2 = table2array(Data(:,10));
LC3 = table2array(Data(:,11));
LCTot = table2array(Data(:,12));
DAQst = table2array(Data(:,16));

LCTot = -LCTot + 92;
Time = Time - 1859500;

%% Plot Data
format shortG

tiledlayout(2, 2);

nexttile;
plot(Time(prst:prfin), PT_O1(prst:prfin));
hold on
plot(Time(prst:prfin), PT_E1(prst:prfin));
title("Tank Pressures");
xlabel("Time (ms)");
ylabel("Pressure (psi)");
legend("LOX Tank", "ETH Tank");
hold off

nexttile;
plot(Time(burnst:burnfin), PT_C1(burnst:burnfin));
title("Chamber Pressure");
xlabel("Time (ms)");
ylabel("Pressure (psi)");

nexttile;
plot(Time(burnst:burnfin), LCTot(burnst:burnfin));
title("Thrust");
xlabel("Time (ms)");
ylabel("Load Cell Sum (lbf) ");


%% Data Analysis

%Size Arrays for Curve Fits
EthSt = 6330;
LOXSt = 0;
Ethfin = 6352;

figure()
plot(Time(EthSt:Ethfin), PT_E1(EthSt:Ethfin));
title("Tank Pressures");
xlabel("Time (ms)");
ylabel("Pressure (psi)");


%% Ethanol

% Convert X and Y into a table, which is the form fitnlm() likes the input data to be in.
% Note: it doesn't matter if X and Y are row vectors or column vectors since we use (:) to get them into column vectors for the table.
EthVals = PT_E1(EthSt:Ethfin)./PT_E1(EthSt);
tbl = table(Time(EthSt:Ethfin), EthVals);
% Define the model as Y = a * exp(-b*x) + c
% Note how this "x" of modelfun is related to big X and big Y.
% x((:, 1) is actually X and x(:, 2) is actually Y - the first and second columns of the table.
modelfun = @(b,x) b(1) * exp(-b(2)*x(:, 1)) + b(3);  

% Guess values to start with.  Just make your best guess.
aGuessed = 1 % Arbitrary sample values I picked.
bGuessed = .5
cGuessed = 50
beta0 = [aGuessed, bGuessed, cGuessed]; % Guess values to start with.  Just make your best guess.

% Now the next line is where the actual model computation is done.
mdl = fitnlm(tbl, modelfun, beta0);
% Now the model creation is done and the coefficients have been determined.
% YAY!!!!

% Extract the coefficient values from the the model object.
% The actual coefficients are in the "Estimate" column of the "Coefficients" table that's part of the mode.
coefficients = mdl.Coefficients{:, 'Estimate'}
% Create smoothed/regressed data using the model:
EthFitted = coefficients(1) * exp(-coefficients(2)*Time) + coefficients(3);

figure()
hold on
plot(Time(EthSt:Ethfin), EthVals);
plot(Time(EthSt:Ethfin), EthFitted(EthSt:Ethfin));
title("Tank Pressures");
xlabel("Time (ms)");
ylabel("Pressure (psi)");
legend("data", "Fit");
hold off
