addpath(strcat(pwd,'/utils'));
datapath='/Users/hzeng/Data/SIF.ql1/';
expno=1;
data0 = readRareFID(datapath, expno);
fid = data0(:,:,1,1);
fid = permute(fid, [2 1]);
ny = size(fid,1);
nx = size(fid,2);
data = fid;

%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% L1 Recon Parameters 
%%%%%%%%%%%%%%%%%%%%%%%%%%%%

pctg = [0.333];  	% undersampling factor
P = 5;              % Variable density polymonial degree

N = size(data); 	% image Size
DN = size(data); 	% data Size
TVWeight = 0.002*1; 	% Weight for TV penalty
xfmWeight = 0.005*1;	% Weight for Transform L1 penalty
Itnlim = 8;         % Number of iterations


% generate variable density random sampling
rng(1000);
pdf1d = genPDF(ny,P,pctg , 1 ,0.1,0);	% generates the sampling PDF
%[pdf,val] = genPDF(imSize,p,pctg [,distType,radius,disp])
mask1d = genSampling(pdf1d,100,2);		% generates a sampling pattern
pdf = repmat(pdf1d(:),[1,nx]);
mask = repmat(mask1d(:),[1,nx]);

%generate Fourier sampling operator
%mask = mask | true;
FT = p2DFT(mask, N, 1, 2);
FTall = p2DFT(true(size(mask)), N, 1, 2);
img  = FTall'*data;
figure
imagesc(abs(img))

% scale data
im_dc = FT'*(data.*mask./pdf);
scale = max(abs(im_dc(:)));
data = data/scale;
im_dc = im_dc/scale;
img = img/scale;


%generate transform operator
%XFM = Wavelet('Daubechies',4,4);	% Wavelet
XFM = Wavelet('Daubechies',4,4);	% Wavelet

% initialize Parameters for reconstruction
param = init;
param.FT = FT;
param.XFM = XFM;
param.TV = TVOP;
param.data = data;
param.TVWeight =TVWeight;     % TV penalty 
param.xfmWeight = xfmWeight;  % L1 wavelet penalty
param.Itnlim = Itnlim;

figure(100), imshow(abs(im_dc),[]);drawnow;

res = XFM*im_dc;

figure;
imshow(abs(res));

%return;
% do iterations
tic
for n=1:5
	res = fnlCg(res,param);
	im_res = XFM'*res;
	figure(100), imshow(abs(im_res),[]), drawnow
end
toc


figure, imshow(abs(cat(2, im_dc,img,im_res, (img-im_res)*5)),[]);
title('  zf-w/dc, full sample, l_1 Wavelet, difference*5', 'fontsize',20);