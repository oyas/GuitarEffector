#include <stdlib.h>
#include <stdint.h>

#include <math.h>

#include <time.h>

#include <pthread.h>

#include <alsa/asoundlib.h>


#define LIMIT_DELAY 0


// 現在時刻を返す
double getTime() {
	struct timespec time_spec;
	clock_getres( CLOCK_REALTIME, &time_spec );
	return (double)( time_spec.tv_sec ) + (double)( time_spec.tv_nsec ) * 0.000001;
}

// 指定された時刻までスリープ
void waitUntil( double _until ) {
	struct timespec until;
	until.tv_sec = (time_t)( _until );
	until.tv_nsec = (long)( ( _until - (time_t)( _until ) ) * 1000000.0 );
	clock_nanosleep( CLOCK_REALTIME, TIMER_ABSTIME, &until, NULL );
}

// 続けるかどうか
char alive=1;

// キー入力を監視するスレッド
void* thread(){
	printf("Working...\nPress Enter to finish :");
	while( getchar() != '\n' ){ }
	printf("finish\n");
	alive = 0;
}

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

// ここから本体
int main( int argc, char *argv[] ) {
	// サウンドデバイスの指定
	const static char device[] = "default";//"hw:0";
	const static char capdevice[] = "default";//"hw:1";
	// 符号付き16bit リトルエンディアン
	const static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	// snd_pcm_readi/snd_pcm_writeiを使って読み書きする
	const static snd_pcm_access_t access = SND_PCM_ACCESS_RW_INTERLEAVED;
	// 再生周波数 48kHz
	const static unsigned int sampling_rate = 48000;
	// モノラル
	const static unsigned int channels = 1;
	// ALSAがサウンドカードの都合に合わせて勝手に再生周波数を変更することを許す
	const static unsigned int soft_resample = 1;
	// 1ミリ秒分ALSAのバッファに蓄える 1000=48フレーム分
	const static unsigned int latency = 20000;
	// バッファのフレーム数
	const unsigned int buffer_size = 24;//sampling_rate / tone_freq;
	// alsa内部のディレイの切り詰めるフレーム数
	const static unsigned int limit_delay = 1000;
	// 過大入力の警告を出す入力レベル(max:0xFFFF)
	const static int warning_level = 0x7AFF;
	// ラ(440Hz)
	const static unsigned int tone_freq = 440;
	// 7秒間流す
	const static unsigned int length = 7;
	// エフェクターが変わる時刻
	int change_time = (argc>1)?atoi(argv[1]):3;
	// ノイズの境界レベル。認識させる最低の音の大きさ
	const static int16_t noise_level = 0x0060;
	// 読み込むファイル( lpcm形式 16bit 48000Hz モノラル リトルエンディアン )
	const static char filename[]="out.lpcm";
	// effecter setting
	float boost_x = 4.0;
	int od_max = 0x4fff;
	float boost_xo = 2.0; //boost

	snd_pcm_t *pcm, *cap;
	// 再生用PCMストリームを開く
	if( snd_pcm_open( &pcm, device, SND_PCM_STREAM_PLAYBACK, 0 ) ) {
		printf( "Unable to open." );
		abort();
	}
	// 録音用を開く
	if( snd_pcm_open( &cap, capdevice, SND_PCM_STREAM_CAPTURE, 0 ) ) {
		printf( "Unable to open capture." );
		abort();
	}

	// 再生周波数、フォーマット、バッファのサイズ等を指定する
	if(
			snd_pcm_set_params(
				pcm, format, access,
				channels, sampling_rate, soft_resample, latency
				)
	  ) {
		printf( "Unable to set format." );
		snd_pcm_close( pcm );
		abort();
	}
	// 録音の方も同様に指定
	if(
			snd_pcm_set_params(
				cap, format, access,
				channels, sampling_rate, soft_resample, latency
				)
	  ) {
		printf( "Unable to set capture format." );
		snd_pcm_close( pcm );
		snd_pcm_close( cap );
		abort();
	}


	// 再生するためのサイン波を作っておく
	int16_t buffer[ buffer_size ];
	int16_t outputbuffer[ buffer_size * 2 ];	//出力用2チャンネルバッファ
	int buffer_iter;
	//for( buffer_iter = 0; buffer_iter != buffer_size; buffer_iter++ )
	//	buffer[ buffer_iter ] = sin( 2.0 * M_PI * buffer_iter / buffer_size ) * 32767;

	// 再生用ファイルを開く
//	FILE *fp;	//ファイルポインタ
//	if ((fp = fopen(filename, "r")) == NULL) {
//		printf("file open error!!\n");
//		exit(EXIT_FAILURE);	/* (3)エラーの場合は通常、異常終了する */
//	}

	// 終了用にスレッド作成
	pthread_t th;
	pthread_create(&th, NULL, thread, NULL);


	// ここから処理ループに入る
	
	double system_time = getTime();
	unsigned int beep_counter = 0;
	int result;
	int timeout=0, sectime=0;
	snd_pcm_sframes_t tmp;
	int limit_pcm=0, limit_cap=0, limit_pcm_t=0;
	int16_t maxvol=0;	//最大入力の調査
	int silenttimer=0;	//ディレイ補正のために停止する時間

//	snd_pcm_link( pcm, cap );

	int err;
//	if ((err = snd_pcm_link( cap, pcm )) < 0) {
//		printf("Streams link error: %s\n", snd_strerror(err));
//		exit(0);
//	}
//	if (snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED){
//		if ((err = snd_pcm_start( pcm )) < 0) {
//			printf("Go pcm error: %s\n", snd_strerror(err));
//			exit(0);
//		}
//	}
	if ((err = snd_pcm_start( cap )) < 0) {
		printf("Go error: %s\n", snd_strerror(err));
		exit(0);
	}

	//for( beep_counter = 0; beep_counter != sampling_rate / buffer_size * length; beep_counter++ ) {
	while( alive ){

		// 少し待ってから書き出す
		waitUntil( system_time + (double)buffer_size / (double)sampling_rate );
		system_time = getTime();
		
		//終了判定
		/*	if( getc(stdin) == 0x1b ){	// Escかどうか
			printf("finish\n");
			break;
			}else{
			putc('a', stdin);
			printf("a");
			}*/
		if( timeout++ > sampling_rate / buffer_size * 3600 ) break;	//1時間で終了

		//デバッグ用
		if( sectime++ > sampling_rate / buffer_size * 5){
			printf("avail");
			tmp = snd_pcm_avail_update( pcm );
			printf("    pcm: %d", (int)tmp);
			tmp = snd_pcm_avail_update( cap );
			printf("    cap: %d\n", (int)tmp);
			printf("delay");
			snd_pcm_delay( pcm, &tmp );
			printf("    pcm: %d", (int)tmp);
			snd_pcm_delay( cap, &tmp );
			printf("    cap: %d\n", (int)tmp);
			sectime=0;
		}

#if LIMIT_DELAY
		// alsaのディレイの調査
		snd_pcm_delay( pcm, &tmp );
		if( (int)tmp > limit_delay ) limit_pcm = 1;
		if( (int)tmp > limit_delay - 100 ) limit_pcm_t = 1;
		snd_pcm_delay( cap, &tmp );
		if( (int)tmp > limit_delay ) limit_cap = 1;
#endif

		// ファイルから読み込む
		//fread(buffer, 1, buffer_size * sizeof(int16_t), fp);

		// 入力から読み取る（録音）
		//	printf("reading...\n");
		result = snd_pcm_readi ( cap, buffer, buffer_size );
		if( result < 0 ) {
			printf("recover %d\n", result);
			printf("EBADFD %d\n", -EBADFD);
			printf("EPIPE %d\n", -EPIPE);
			printf("ESTRPIPE %d\n", -ESTRPIPE);
			printf("SND_PCM_STATE_PREPARED %d\n", SND_PCM_STATE_PREPARED);
			printf("SND_PCM_STATE_DRAINING %d\n", SND_PCM_STATE_DRAINING);
			// バッファアンダーラン等が発生してストリームが停止した時は回復を試みる
			if( snd_pcm_recover( cap, result, 0 ) < 0 ) {
				printf( "Unable to recover this stream. (capture)" );
				snd_pcm_close( pcm );
				snd_pcm_close( cap );
				abort();
			}
		}

#if LIMIT_DELAY
		// alsaのディレイが大きい時
		if( limit_cap ){
			limit_cap = 0;
			continue;
		}
#endif

		// ノイズ消去
		for( buffer_iter = 0; buffer_iter != buffer_size; buffer_iter++ ){
			maxvol = (maxvol > buffer[buffer_iter]) ? maxvol: buffer[buffer_iter] ;
			if( buffer[ buffer_iter ] > 0 ){
				buffer[ buffer_iter ] = ( noise_level > buffer[buffer_iter]) ? 0: buffer[buffer_iter];
			}else{
				buffer[ buffer_iter ] = ( -noise_level < buffer[buffer_iter]) ? 0: buffer[buffer_iter];
			}
		}

		// エフェクターをかける
		if( 1 ){//beep_counter > sampling_rate / buffer_size * change_time ){
			// over drive
			for( buffer_iter = 0; buffer_iter != buffer_size; buffer_iter++ ){
				maxvol = (maxvol > buffer[buffer_iter]) ? maxvol: buffer[buffer_iter] ;
				buffer[ buffer_iter ] = Booster( buffer[ buffer_iter ], boost_xo);
		//		buffer[ buffer_iter ] = Overdrive( buffer[ buffer_iter ], boost_x, od_max );
			}
		}else{
			// boost
			for( buffer_iter = 0; buffer_iter != buffer_size; buffer_iter++ ){
				maxvol = (maxvol > buffer[buffer_iter]) ? maxvol: buffer[buffer_iter] ;
				if( buffer[ buffer_iter ] > warning_level ){
					// 過大入力の警告
					printf("WARNING!! Input level is too large!\n");
				}
				buffer[ buffer_iter ] = Booster( buffer[ buffer_iter ], boost_x);
				//output
				outputbuffer[ buffer_iter * 2     ] = buffer[ buffer_iter ];
				outputbuffer[ buffer_iter * 2 + 1 ] = buffer[ buffer_iter ];
			}
		}

#if LIMIT_DELAY
		// alsaのディレイが大きい時は書き出さない
		if( limit_pcm || silenttimer){
		//	snd_pcm_drop( pcm );
		//	snd_pcm_start( pcm );
		//	waitUntil( system_time + (double)100 / (double)1000000 );
		//	system_time = getTime();
			if( silenttimer && !limit_pcm_t ){
				silenttimer = 0;
			}else{
				silenttimer = 1;
			}
			limit_pcm = 0;
			limit_pcm_t = 0;
		}else{
#endif
			// buffer_size分だけ書き出す
			result = snd_pcm_writei ( pcm, ( const void* )buffer, buffer_size );
			//result = snd_pcm_writei ( pcm, ( const void* )outputbuffer, buffer_size * 2 );
			if( result < 0 ) {
				printf("recover\n");
				// バッファアンダーラン等が発生してストリームが停止した時は回復を試みる
				if( snd_pcm_recover( pcm, result, 0 ) < 0 ) {
					printf( "Unable to recover this stream." );
					snd_pcm_close( pcm );
					snd_pcm_close( cap );
					abort();
				}
			}

#if LIMIT_DELAY
		}
#endif

		// デバッグ
		if( sectime == 0 ){
			printf("delay after");
			snd_pcm_delay( pcm, &tmp );
			printf("    pcm: %d", (int)tmp);
			snd_pcm_delay( cap, &tmp );
			printf("    cap: %d\n", (int)tmp);
			printf("MAXVOLUME : %d\%% %d\n", maxvol * 100 / 0x7FFF, maxvol );
			maxvol = 0;
		}
	}

	//file close
	//	fclose(fp);

	snd_pcm_drop( pcm );
	snd_pcm_drop( cap );

//	snd_pcm_unlink( cap );

	// 終わったらストリームを閉じる
	snd_pcm_close( pcm );
	snd_pcm_close( cap );

	pthread_join(th, NULL);

	return 0;
}
