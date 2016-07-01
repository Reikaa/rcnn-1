#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <sstream>
#include <algorithm>

//#include <omp.h>
#ifdef LINUX
#include <sys/time.h>
#else
#include <time.h>
#endif

using namespace std;
typedef vector<int> doc;

#include "fileutil.hpp"

const int H = 50; //���ز� 50
const int H1 = 150; //���ز㣨H*2+WV�� 150
const int H2 = 100; //���ز� 100
//const int H = 100; //���ز� 50
//const int H1 = 500; //���ز㣨H*2+WV�� 150
//const int H2 = 100; //���ز� 100
const int MAX_C = 50; //��������
const int MAX_F = 900; //��������Ĵ�С

int class_size = 4; //������ //TODO �����Ϣ���Դ��������Զ���ȡ
int window_size = 5; //���ڴ�С
bool mr = false;

const char *model_name = "model_300_nosuff_noinit";

const char *train_file = "E:\\ecnn����\\��������\\train.txt";
const char *valid_file = NULL;
const char *test_file = "E:\\ecnn����\\��������\\test.txt";
const char *dict_file = "dict.txt";


int input_size; //���ڴ�С
int vector_size; //һ���ʵ�Ԫ��������С = ��������С��Լ50�� + ���������Ĵ�С��Լ10��

const int thread_num = 16;
//===================== ����Ҫ�Ż��Ĳ��� =====================


embedding_t words; //������
//embedding_t words_o; //������

double *A; //��������[������][���ز�] �ڶ����Ȩ��
double *B_l, *B_r; //��������[���ز�][������] ��һ���Ȩ��
double *C;
double *gA, *gB_l, *gB_r;

double biasOutput[20]; //classsize

//===================== ��֪���� =====================

//ѵ����
vector<doc> data; //ѵ�����ݣ�[������][������]
//int N; //ѵ������С
//int uN; //δ֪��
vector<int> b; //Ŀ�����[������] ѵ����

//��֤��
vector<doc> vdata; //�������ݣ�[������][������]
//int vN; //���Լ���С
//int uvN; //δ֪��
vector<int> vb; //Ŀ�����[������] ���Լ�

//���Լ�
vector<doc> tdata; //�������ݣ�[������][������]
//int tN; //���Լ���С
//int utN; //δ֪��
vector<int> tb; //Ŀ�����[������] ���Լ�




double time_start;
double lambda = 0;//0.01; //���������Ȩ��
double alpha = 0.01; //ѧϰ����
int iter = 0;

double getTime() {
#ifdef LINUX
	timeval tv;
	gettimeofday(&tv, 0);
	return tv.tv_sec + tv.tv_usec * 1e-6;
#else
	return 1.0*clock() / CLOCKS_PER_SEC;
#endif
}

double nextDouble() {
	return rand() / (RAND_MAX + 1.0);
}

void softmax(double hoSums[], double result[], int n) {
	double max = hoSums[0];
	for (int i = 0; i < n; ++i)
	if (hoSums[i] > max) max = hoSums[i];
	double scale = 0.0;
	for (int i = 0; i < n; ++i)
		scale += exp(hoSums[i] - max);
	for (int i = 0; i < n; ++i)
		result[i] = exp(hoSums[i] - max) / scale;
}

double sigmoid(double x) {
	return 1 / (1 + exp(-x));
}

double hardtanh(double x) {
	if (x > 1)
		return 1;
	if (x < -1)
		return -1;
	return x;
}

//b = Ax
void fastmult(double *A, double *x, double *b, int xlen, int blen) {
	double val1, val2, val3, val4;
	double val5, val6, val7, val8;
	int i;
	for (i = 0; i < blen / 8 * 8; i += 8) {
		val1 = 0;
		val2 = 0;
		val3 = 0;
		val4 = 0;

		val5 = 0;
		val6 = 0;
		val7 = 0;
		val8 = 0;

		for (int j = 0; j < xlen; j++) {
			val1 += x[j] * A[j + (i + 0)*xlen];
			val2 += x[j] * A[j + (i + 1)*xlen];
			val3 += x[j] * A[j + (i + 2)*xlen];
			val4 += x[j] * A[j + (i + 3)*xlen];

			val5 += x[j] * A[j + (i + 4)*xlen];
			val6 += x[j] * A[j + (i + 5)*xlen];
			val7 += x[j] * A[j + (i + 6)*xlen];
			val8 += x[j] * A[j + (i + 7)*xlen];
		}
		b[i + 0] += val1;
		b[i + 1] += val2;
		b[i + 2] += val3;
		b[i + 3] += val4;

		b[i + 4] += val5;
		b[i + 5] += val6;
		b[i + 6] += val7;
		b[i + 7] += val8;
	}

	for (; i < blen; i++) {
		for (int j = 0; j < xlen; j++) {
			b[i] += x[j] * A[j + i*xlen];
		}
	}
}

const int max_doc_length = 10000; //100000
double ah_l[max_doc_length][H];//���е����ز㡣����ĵ�����*H
double ah_r[max_doc_length][H];//���е����ز㡣����ĵ�����*H
double ax_l[max_doc_length][MAX_F]; //����
double ax_r[max_doc_length][MAX_F]; //����
double ax_f[max_doc_length][MAX_F]; //ȫ��
double h0_l[H];
double h0_r[H];
const int bptt = 3;

int start_word = 0;
int end_word = 0;

void BPTT(int p, double *dx, doc &id, double *h0, double ah[][H],
	double ax[][MAX_F], double *B, double *gB, bool left) {

	int _start_word = 0;
	if (left)
		_start_word = start_word;
	else
		_start_word = id.size() - end_word;

	for (int k = p; k >= _start_word && k > p - bptt; k--) {
		if (k == _start_word) {
			for (int i = 0; i < H; i++) {
				h0[i] += alpha * (dx[i + words.element_size] - lambda *  h0[i]);
			}
			continue;
		}

		//�޸Ĵ�����
		int offset = id[left ? k : (int)id.size() - 1 - k] * words.element_size;
		for (int j = 0; j < words.element_size; j++) {
			int t = offset + j;
			words.value[t] += alpha * (dx[j] - lambda * (words.value[t]));
		}

		//ǰһ�����ز�
		double dh[H];
		for (int i = 0; i < H; i++) {
			dh[i] = dx[i + words.element_size];
		}

		if (k >= 1) { //ֻ��ǰ���ж������Żش�
			double *h = ah[k];
			double *x = ax[k - 1];

			for (int i = 0; i < H; i++) {
				//dh[i] *= 1 - h[i] * h[i];
				if (h[i] <= 0)
					dh[i] = 0;
			}

			//�������ز��ݶ�
			//double dx[MAX_F] = { 0 };
			for (int j = 0; j < input_size; j++) {
				dx[j] = 0;
			}

			for (int i = 0; i < H; i++) {
				for (int j = 0; j < input_size; j++) {
					int t = i*input_size + j;
					dx[j] += dh[i] * B[t];
					gB[t] += x[j] * dh[i];
				}
			}
		}



		//
	}
}
//const int K = 3;
double checkCase(doc &id, int ans, int &correct, int &output, bool gd = false) {
	//	double x[MAX_F];
	//int hw = (window_size - 1) / 2;
	double h[H2];
	int maxhi[H2];
	//double hx[H][MAX_F];
	//for (int k = 0; k < K; k++)
	for (int j = 0; j < H2; j++)  //Ҫȡmax����ʼ����-inf
		h[j] = -1e300;

	if ((int)id.size() > max_doc_length) {
		printf("too long doc length: %lu\n", id.size());
	}
	//while (id.size() < 3) {
	//	id.push_back(0);
	//}

	start_word = 0;
	end_word = (int)id.size();
	if (gd) {
		int sl = (int)(nextDouble()*nextDouble()* id.size() + 0.5);
		if (sl < 3)
			sl = 3;
		if (sl >= end_word) { //size
			//����̫�̣������ж�
		} else {
			start_word = rand() % (end_word - sl + 1);
			end_word = start_word + sl;
		}
	}

	for (int j = 0; j < H; j++)
		ah_l[start_word][j] = h0_l[j]; //TODO ���Ҫ�ĳ�ĳ����ʼֵ

	//printf("%d %d %d\n", (int)id.size(), start_word, end_word);
	//��������һ�����ز�
	for (int i = start_word; i < end_word; i++) { //�����������ģ�ǰ���Ѿ�����padding��
		double *x = ax_l[i]; //��ǰ��Ӧ�������
		double *th = ah_l[i + 1]; //��ǰ��Ӧ�����ز�

		//x�ĵ�һ����Ϊ��ǰ�Ĵʵ�embedding
		//printf("%d\t", id[i]);
		int offset = id[i] * words.element_size;
		for (int j = 0; j < words.element_size; j++) {
			x[j] = words.value[offset + j];
		}

		//x�ĵڶ�����Ϊ֮ǰ�����ز�
		for (int j = 0; j < H; j++) {
			x[j + words.element_size] = ah_l[i][j];
		}

		for (int j = 0; j < H; j++)
			th[j] = 0;

		fastmult(B_l, x, th, input_size, H);

		for (int j = 0; j < H; j++) {
			th[j] = max(0.0, th[j]);
		}
	}


	for (int j = 0; j < H; j++)
		ah_r[(int)id.size() - end_word][j] = h0_r[j]; //TODO ���Ҫ�ĳ�ĳ����ʼֵ

	//��������һ�����ز�
	for (int i = (int)id.size() - end_word; i < (int)id.size() - start_word; i++) { //�����������ģ�ǰ���Ѿ�����padding��
		double *x = ax_r[i]; //��ǰ��Ӧ�������
		double *th = ah_r[i + 1]; //��ǰ��Ӧ�����ز�

		//x�ĵ�һ����Ϊ��ǰ�Ĵʵ�embedding
		int offset = id[id.size() - 1 - i] * words.element_size;
		for (int j = 0; j < words.element_size; j++) {
			x[j] = words.value[offset + j];
		}

		//x�ĵڶ�����Ϊ֮ǰ�����ز�
		for (int j = 0; j < H; j++) {
			x[j + words.element_size] = ah_r[i][j];
		}

		for (int j = 0; j < H; j++)
			th[j] = 0;

		fastmult(B_r, x, th, input_size, H);

		for (int j = 0; j < H; j++) {
			th[j] = max(0.0, th[j]);
		}
	}




	//��������һ�����ز�
	for (int i = start_word; i < end_word; i++) { //�����������ģ�ǰ���Ѿ�����padding��
		double *x = ax_f[i]; //��ǰ��Ӧ�������

		int offset = id[i] * words.element_size;
		for (int j = 0; j < words.element_size; j++) {
			x[j] = words.value[offset + j];
		}
		for (int j = 0; j < H; j++) {
			x[j + words.element_size] = ah_l[i][j];
		}
		for (int j = 0; j < H; j++) {
			x[j + words.element_size + H] = ah_r[id.size() - i - 1][j];
		}

		double th2[H2] = { 0 };
		fastmult(C, x, th2, H1, H2);

		for (int j = 0; j < H2; j++) {
			if (th2[j] > h[j]) {
				h[j] = th2[j];
				maxhi[j] = i; //������λ��
			}
		}
	}

	for (int i = 0; i < H2; i++) {
		h[i] = tanh(h[i]);
	}

	double r[MAX_C] = { 0 };
	for (int i = 0; i < class_size; i++) {
		r[i] = biasOutput[i];
		for (int j = 0; j < H2; j++) {
			r[i] += h[j] * A[i*H2 + j];
		}
	}
	double y[MAX_C];
	softmax(r, y, class_size);

	double dy = ans - r[0];

	if (gd) { //�޸Ĳ���
		double dh[H2] = { 0 };
		if (class_size == 1) { //�ع�����
			biasOutput[0] += alpha * (dy - lambda*biasOutput[0]);
			for (int j = 0; j < H2; j++) {
				dh[j] += dy * A[j];
				//dh[j] *= 1 - h[j] * h[j];
			}
		} else {
			for (int i = 0; i < class_size; i++) {
				if (i == ans) {
					biasOutput[i] += alpha*(1 - y[i] - lambda*biasOutput[i]);
				} else {
					biasOutput[i] += alpha*(0 - y[i] - lambda*biasOutput[i]);
				}
			}
			for (int j = 0; j < H2; j++) {
				dh[j] = A[ans*H2 + j];
				for (int i = 0; i < class_size; i++) {
					dh[j] -= y[i] * A[i*H2 + j];
				}
				dh[j] *= 1 - h[j] * h[j];
				//dh[j] *= h[j]*(1-h[j]);
				/*if(h[j] > 1 || h[j] < -1)
				dh[j] = 0;
				biasH[j] += alpha * dh[j];*/

			}
		}


		//#pragma omp critical
		{
			for (int i = 0; i < class_size; i++) {
				double v = (i == ans ? 1 : 0) - y[i];
				for (int j = 0; j < H2; j++) {
					int t = i*H2 + j;
					A[t] += alpha / sqrt(H2) * (v * h[j] - lambda * A[t]);
					//gA[i*H+j] += v * h[j];
				}
			}

			//double dx[MAX_F] = { 0 };

			//fastmult(B, dh, dx, input_size, H);

			for (int i = 0; i < H*input_size; i++) {
				gB_l[i] = 0;
				gB_r[i] = 0;
			}

			for (int i = 0; i < H2; i++) {
				int p = maxhi[i];
				double dh1[MAX_F] = { 0 }; //ֻ�洢ĳ��λ�õ��ݶ�

				for (int j = 0; j < H1; j++) {
					dh1[j] = dh[i] * C[i*H1 + j];
				}

				double dh_l[MAX_F], dh_r[MAX_F];
				for (int j = 0; j < words.element_size; j++) {
					dh_l[j] = dh1[j] / 2;
					dh_r[j] = dh1[j] / 2;
				}
				for (int j = 0; j < H; j++) {
					dh_l[j + words.element_size] = dh1[j + words.element_size];
				}
				for (int j = 0; j < H; j++) {
					dh_r[j + words.element_size] = dh1[j + words.element_size + H];
					//x[j + words.element_size + H] = ah_r[i][j];
				}

				//dh1[i] = dh[i]; //��ǰֻ�������ά�ȵ�bp
				BPTT(p, dh_l, id, h0_l, ah_l, ax_l, B_l, gB_l, true);
				BPTT(id.size() - 1 - p, dh_r, id, h0_r, ah_r, ax_r, B_r, gB_r, false);
			}

			for (int i = 0; i < H2; i++) {
				int p = maxhi[i];

				for (int j = 0; j < H1; j++) {
					C[i*H1 + j] += alpha / sqrt(H1) * (dh[i] * ax_f[p][j] - lambda*C[i*H1 + j]);
				}
			}


			for (int i = 0; i < H; i++) {
				for (int j = 0; j < input_size; j++) {
					int t = i*input_size + j;
					B_l[t] += alpha / sqrt(input_size) * (gB_l[t] - lambda * B_l[t]);
					B_r[t] += alpha / sqrt(input_size) * (gB_r[t] - lambda * B_r[t]);
				}
			}

		}
	}

	output = 0;
	double maxi = 0;
	bool ok = true;
	for (int i = 0; i < class_size; i++) {
		if (i != ans && y[i] >= y[ans])
			ok = false;
		if (y[i] > maxi) {
			maxi = y[i];
			output = i;
		}
		//if (p)
		//	p[i] = -log(y[i]);
	}

	if (ok)
		correct++;
	if (class_size == 1)
		return dy*dy;
	return log(y[ans]); //������Ȼ
}


void writeFile(const char *name, double *A, int size) {
	FILE *fout = fopen(name, "wb");
	fwrite(A, sizeof(double), size, fout);
	fclose(fout);
}

double checkSet(const char *dataset, vector<doc> &data, vector<int> &b, char *fname = NULL) {
	int N = (int)data.size();

	double ret = 0;
	int wordCorrect = 0; //ֱ�ӵĴ�׼ȷ��


	int sc[20][3] = { { 0 } };
	//[c][0] Ŀ��=��=c
	//[c][1] Ŀ��=c ��!=c �ٻ���
	//[c][2] Ŀ��!=c ��=c ׼ȷ��

#pragma omp parallel for schedule(dynamic) num_threads(thread_num)

	for (int s = 0; s < N; s++) {
		int tc = 0;
		int output;
		double tv = checkCase(data[s], b[s], tc, output);

#pragma omp critical
		{
			ret += tv;
			if (output == b[s]) {
				sc[b[s]][0]++;
			} else {
				sc[b[s]][1]++;
				sc[output][2]++;
			}
			//printf("%lf, ", ret);
			wordCorrect += tc;
		}
	}
	double ff = 0;
	for (int i = 0; i < class_size; i++) {
		//printf("%d %d %d\n", sc[i][0], sc[i][1], sc[i][2]);
		double p = sc[i][0] == 0 ? 0 : 1.0* sc[i][0] / (sc[i][0] + sc[i][2]);
		double r = sc[i][0] == 0 ? 0 : 1.0*sc[i][0] / (sc[i][0] + sc[i][1]);
		double f = sc[i][0] == 0 ? 0 : 2 * p * r / (p + r);
		ff += f;
	}
	printf("%s:%lf(%.2lf%%,%.2lf%%), ", dataset, -ret / N,
		100.*wordCorrect / N, ff / class_size * 100);
	return -ret / N;
}

//�����ȷ�ʺ���Ȼ
//����ֵ����Ȼ
double check() {
	double ret = 0;

	double ps = 0;
	int pnum = 0;
	for (int i = 0; i < class_size*H2; i++, pnum++) {
		ps += A[i] * A[i];
	}
	for (int i = 0; i < H*input_size; i++, pnum++) {
		//	ps += B[i] * B[i];
	}
	for (int i = 0; i < words.size; i++, pnum++) {
		ps += (words.value[i]) * (words.value[i]);
	}

	char fname[100];
	sprintf(fname, "%s_%d_A", model_name, iter);
	writeFile(fname, A, class_size*H2);
	sprintf(fname, "%s_%d_Bl", model_name, iter);
	writeFile(fname, B_l, H*input_size);
	sprintf(fname, "%s_%d_Br", model_name, iter);
	writeFile(fname, B_r, H*input_size);
	sprintf(fname, "%s_%d_C", model_name, iter);
	writeFile(fname, C, input_size*H2);
	sprintf(fname, "%s_%d_w", model_name, iter);
	writeFile(fname, words.value, words.size);
	sprintf(fname, "%s_%d_h0l", model_name, iter);
	writeFile(fname, h0_l, H);
	sprintf(fname, "%s_%d_h0r", model_name, iter);
	writeFile(fname, h0_r, H);
	sprintf(fname, "%s_bias", model_name);
	writeFile(fname, biasOutput, class_size);

	printf("para: %lf, ", ps / pnum / 2);

	ret = checkSet("train", data, b);
	checkSet("valid", vdata, vb);
	//sprintf(fname, "%s_%d_output", model_name, iter);
	checkSet("test", tdata, tb);

	printf("time:%.1lf\n", getTime() - time_start);
	fflush(stdout);

	double fret = ret + ps / pnum*lambda / 2;
	return fret;
}

int readFile(const char *name, double *A, int size) {
	FILE *fin = fopen(name, "rb");
	if (!fin)
		return 0;
	int len = (int)fread(A, sizeof(double), size, fin);
	fclose(fin);
	return len;
}

//��һ����������������ĵ���һ��embedding��ƽ��ֵ
void SimplifyData(vector<doc> &data) {
	for (size_t i = 0; i < data.size(); i++) {
		doc &d = data[i];
		sort(d.begin(), d.end());
		d.erase(unique(d.begin(), d.end()), d.end());
	}
}

void SimplifyDataWordCh(vector<doc> &data) {
	int total = 0;
	for (size_t i = 0; i < data.size(); i++) {
		doc &d = data[i];

		doc lst;
		for (int j = 0; j < (int)d.size(); j++) {
			WordCh wc((char*)vocab[d[j]].c_str());
			int cnt = 0;
			while (char *cc = wc.NextCh()) { //������е�ÿ����
				if (dict.count(cc)) {
					lst.push_back(dict[cc]);
					cnt++;
				}
			}
			if (cnt != 1) {
				lst.push_back(d[j]);
			}
		}

		d = lst;

		sort(d.begin(), d.end());
		d.erase(unique(d.begin(), d.end()), d.end());

		total += d.size();
	}
	printf("total=%d\n", total);
}

/*
ѵ���������Լ�����֤�� vec<doc>
doc=vec<word>
word=(string)ch~ch~ch

����
embedding ÿ���ʡ��ֶ���һ����ĳ��hash���Ӧ

���� doc�����ط��ࣨ�����м�������������ݶȣ�


*/
//��һ����������������ĵ���һ��embedding��ƽ��ֵ
void AddPadding(vector<doc> &data) {
	/*for (size_t i = 0; i < data.size(); i++) {
	doc &d = data[i];
	sort(d.begin(), d.end());
	d.erase(unique(d.begin(), d.end()), d.end());
	}*/
	int hw = (window_size - 1) / 2;

	for (size_t i = 0; i < data.size(); i++) {
		doc &d = data[i];
		doc dd;
		dd.reserve(d.size() + 2 * hw);
		for (int i = 0; i < hw; i++)
			dd.push_back(0);
		for (size_t j = 0; j < d.size(); j++) {
			dd.push_back(d[j]);
		}
		for (int i = 0; i < hw; i++)
			dd.push_back(0);
		d = dd;
	}
}

int main(int argc, char **argv) {
	if (argc < 5) {
		printf("Useage: ./ecnn w(null) train test class_size rand_seed mr(1) rand_init(1) valid(90=90%%train)\n");
		return 0;
	}
	for (int i = 0; i < argc; i++) {
		printf("%s ", argv[i]);
	}
	printf("\n");
	model_name = argv[2];

	train_file = argv[2];
	test_file = argv[3];

	class_size = atoi(argv[4]);
	if (class_size == 1) { //�ع��ʱ���ر����ײ���������һ��Ҫ�����
		lambda = 0.01;
	}
	srand(atoi(argv[5]));

	if (strcmp(argv[6], "1") == 0)
		mr = true;
	else if (strcmp(argv[6], "0") == 0)
		mr = false;
	else
		valid_file = argv[6];

	//printf("read embedding\n");


	printf("read data\n");
	if (valid_file == NULL) {
		ReadAllFiles(train_file, test_file, atoi(argv[8]), NULL, argv[1], 0, words,
			data, b, vdata, vb, tdata, tb);
	} else {
		ReadAllFiles(train_file, test_file, -1, valid_file, argv[1], 0, words,
			data, b, vdata, vb, tdata, tb);
	}

	{
		printf("initialized with %s\n", argv[1]);
		double sum = 0;
		for (int i = 0; i < words.size; i++) {
			sum += words.value[i] * words.value[i];
		}
		sum = sqrt(sum / words.size * 12);
		for (int i = 0; i < words.size; i++) {
			words.value[i] /= sum;
		}
		//�����ʼ��embedding
		if (atoi(argv[7]) == 1) {
			for (int i = 0; i < words.size; i++) {
				words.value[i] = (nextDouble() - 0.5);
			}
			printf("rand initialized\n");
		}
	}
	printf("H:%d, H1:%d, H2:%d\n", H, H1, H2);
	//words_o = words;
	//words_o.value = new double[words_o.size];
	//memcpy(words_o.value, words.value, sizeof(double)*words.size);
	//lambda = 0.001;

	input_size = words.element_size + H;

	A = new double[class_size*H2];
	//gA = new double[class_size*H];
	C = new double[H1*H2];
	B_l = new double[H*input_size];
	B_r = new double[H*input_size];

	gB_l = new double[H*input_size];
	gB_r = new double[H*input_size];

	for (int i = 0; i < class_size * H2; i++) {
		A[i] = (nextDouble() - 0.5) / sqrt(H2);
	}
	/*for (int i = 0; i < H * input_size; i++) {
		B[i] = (nextDouble() - 0.5) / sqrt(input_size);
		}*/
	for (int i = 0; i < H1 * H2; i++) {
		C[i] = (nextDouble() - 0.5) / sqrt(H1);
	}
	for (int i = 0; i < H; i++) {
		for (int j = 0; j < input_size; j++) {
			if (i == j || j == i + H) {
				B_l[i*input_size + j] = 0.5;
				B_r[i*input_size + j] = 0.5;
			} else {
				B_l[i*input_size + j] = 0;
				B_r[i*input_size + j] = 0;
			}
		}
	}

	//readFile("ECCC_1", B, H*input_size);
	//printf("read data\n");
	//ReadDocs(train_file, data, b, "Train");
	//

	//SimplifyData(data);
	//SimplifyData(tdata);
	//AddPadding(data); ѭ�����粻��Ҫ��padding
	//


	time_start = getTime();

	int N = data.size();
	int *order = new int[N];
	for (int i = 0; i < N; i++) {
		order[i] = i;
	}

	//srand(atoi(argv[5])); //��֤ͬ���������зֵ����ݼ���һ�µ�

	//if (mr) {
	//	for (int i = N * 9 / 10; i < N; i++) {
	//		tdata.push_back(data[i]);
	//		tb.push_back(b[i]);
	//	}
	//	data.erase(data.begin() + N * 9 / 10, data.end());
	//	b.erase(b.begin() + N * 9 / 10, b.end());
	//	N = data.size();
	//} else {
	//	//ReadDocs(test_file, tdata, tb, "Test");
	//	//AddPadding(tdata);
	//}



	printf("%lu, %lu, %lu\n", data.size(), vdata.size(), tdata.size());
	//double lastLH = 1e100;
	while (iter < 300) {
		//������ȷ��
		printf("%citer: %d, ", 13, iter);
		//double LH = check();
		//updateWordsExists();
		//if (iter)
		check();
		iter++;
		/*if(LH > lastLH){
			alpha = 0.0001;
			}
			lastLH = LH;*/


		double lastTime = getTime();
		//memset(gA, 0, sizeof(double)*class_size*H);
		//memset(gB, 0, sizeof(double)*H*input_size);

		for (int i = 0; i < N; i++) {
			swap(order[i], order[rand() % N]);
		}
		//double tlambda = lambda;
		double err0 = 0;
		int cnt = 0;
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
		for (int i = 0; i < N; i++) {
			//lambda = 0;
			//if (i % 10 == 0)
			//	lambda = tlambda;
			int s = order[i];
			//data_t *x = data + s*window_size;
			int ans = b[s];

			int tmp, output;
			double terr = checkCase(data[s], ans, tmp, output, true);
#pragma omp critical
			{
				cnt++;
				err0 += terr;
				if ((cnt % 10) == 0) {
					//printf("%cIter: %3d\t   Progress: %.2f%%   Err: %.2lf   Words/sec: %.1f ",
					//	13, iter, 100.*cnt / N, err0 / (cnt + 1), cnt / (getTime() - lastTime));
				}
			}
		}
		//lambda = tlambda;
		//for(int i = 0; i < vN; i++){
		//	int s = i;
		//	data_t *x = vdata + s * window_size;
		//	int ans = vb[s];
		//	int tmp;
		//	checkCase(x, ans, tmp, true);

		//	if ((i%100)==0){
		//	//	printf("%cIter: %3d\t   Progress: %.2f%%   Words/sec: %.1f ", 13, iter, 100.*i/N, i/(getTime()-lastTime));
		//	}
		//}
		//printf("%c", 13);
	}
	return 0;
}