#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


// オーバーフロー防止付き掛け算
int16_t Booster(int16_t input, float x){
	if( input > 0 ){
		return ( input > (float)0x7fff / x )?0x7fff:(float)input*x;
	}else{
		return ( input < -(float)0x7fff / x )?-0x7fff:(float)input*x;
	}
}

// オーバードライブ
int16_t Overdrive(int16_t input, float x, int16_t max){
	if( input > 0 ){
		return ( input > (float)max / x )?max:(float)input*x;
	}else{
		return ( input < -(float)max / x / 2.0 )?-max/2:(float)input*x/2.0;
	}
}

//ファイル保存 (sizeはバイト数)
int savewave(int16_t *buffer, unsigned int size, const char *filename){
	FILE *fp;
	int filesize = size + 42;	//ファイル全体のサイズ(本体＋ヘッダ)
	//開く
	if( (fp = fopen(filename, "w")) == NULL ){
		printf("save file open error!\n");
		exit(EXIT_FAILURE);
	}

	//書き込み waveヘッダ
	fprintf(fp, "RIFF");		//RIFFヘッダ
	fwrite( &filesize , 4, 1, fp);	//ファイルサイズ
	fprintf(fp, "WAVE");		//WAVEヘッダ
	fprintf(fp, "fmt ");		//fmt チャンク
	fprintf(fp, "%c%c%c%c", 0x12, 0x00, 0x00, 0x00);	//fmt チャンクのバイト数(18byte)
	fprintf(fp, "%c%c", 0x01, 0x00);	//フォーマットID(リニアPCM)
	fprintf(fp, "%c%c", 0x01, 0x00);	//チャンネル数(1 モノラル)
	fprintf(fp, "%c%c%c%c", 0x80, 0xbb, 0x00, 0x00);	//サンプリングレート(48000Hz)
	fprintf(fp, "%c%c%c%c", 0x00, 0x77, 0x01, 0x00);	//データ速度(byte/s)
	fprintf(fp, "%c%c", 0x02, 0x00);	//ブロックサイズ(Byte/sample×チャンネル数)
	fprintf(fp, "%c%c", 0x10, 0x00);	//bit/sample (16bitならば0x1000)
	fprintf(fp, "%c%c", 0x00, 0x00);	//拡張部分のサイズ
	fprintf(fp, "data");	//dataチャンク
	fwrite( &size , 4, 1, fp);	//dataのサイズ

	// 書き込み データ本体
	fwrite( buffer, size, 1, fp );
	
	//閉じる
	fclose(fp);
	
	return 0;
}

// ここから本体
int main( int argc, char *argv[] ) {
	// 符号付き16bit モノラル リトルエンディアン
	// 再生周波数 48kHz
	const static unsigned int sampling_rate = 48000;
	// エフェクターが変わる時刻
	int change_time = (argc>1)?atoi(argv[1]):3;
	// 読み込むファイル( lpcm形式 16bit 48000Hz モノラル リトルエンディアン )
	const static char filename[]="guitar.lpcm";
	// 保存ファイル名
	const static char savefilename[]="savefile.wav";
	// effecter setting
	float boost_x = 3.0;
	int od_max = 0x2fff;
	float boost_xo = 4.0;


	// 再生するためのバッファ
	int16_t buffer;

	// 再生用ファイルを開く
	FILE *fp;	//ファイルポインタ
	if ((fp = fopen(filename, "r")) == NULL) {
		printf("file open error!!\n");
		exit(EXIT_FAILURE);	/* (3)エラーの場合は通常、異常終了する */
	}

	//ファイルサイズ取得
	fseek(fp, 0, SEEK_END);
	int size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	//書き込み用バッファ確保
	int16_t *writebuffer = (int16_t *)malloc( size );//+ buffer_size*sizeof(int16_t) );

	unsigned int i = 0;
	for( i = 0; i < size / sizeof(int16_t); i++ ) {
		// ファイルから読み込む
		fread(&buffer, 1, sizeof(int16_t), fp);
		
		// エフェクターをかける
		if( i > sampling_rate * change_time ){
			// over drive
				buffer = Booster( buffer, boost_xo);
				buffer = Overdrive( buffer, boost_x, od_max );
		}else{
			// boost
				buffer = Booster( buffer, boost_x);
		}

		//ファイル保存用バッファに書き出す
		writebuffer[ i ] = buffer;

	}

	// ファイルを閉じる
	fclose(fp);
	
	//保存
	savewave(writebuffer, size, savefilename);

	//メモリ解放
	free( writebuffer );
}
