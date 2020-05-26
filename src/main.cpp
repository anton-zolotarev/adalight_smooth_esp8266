#include <Arduino.h>

#include <FastLED.h>
#include <ESP8266WiFi.h>
/*
	 Управление лентой на WS2812 с компьютера + динамическая яркость
	 Создано не знаю кем, допилил и перевёл AlexGyver http://alexgyver.ru/
	 2017
*/
//----------------------НАСТРОЙКИ-----------------------
#define NUM_LEDS    234     // число светодиодов в ленте
#define LED_TYPE    WS2812B // led strip type for FastLED
#define LED_COLOR   GRB     // color order
#define LED_PIN     2      // пин, к которому подключена лента

#define serialRate 256000  // скорость связи с ПК
#define serialTimeout 10   // время по истечении которого выключаем светодиоды

#define smoothFPS 50
#define smoothStep 50
#define deltaMin 3
#define deltaMax 10
//----------------------НАСТРОЙКИ-----------------------
//#define FASTLED_ALLOW_INTERRUPTS 0

enum {
	_HEAD_1 = 0,
	_HEAD_2,
	_HEAD_3,
	_HEAD_HI,
	_HEAD_LO,
	_HEAD_CHK,
	_HEAD_PKG,
	_HEAD_OK
};

#define PACKET_SIZE (3 * (256 * (unsigned)header[_HEAD_HI] + (unsigned)header[_HEAD_LO]))

const char *ack = "Ada\n";
uint8_t header[] = {'A', 'd', 'a', 0, 0, 0};  // кодовое слово Ada для связи
CRGB leds[NUM_LEDS];
CRGB leds_new[NUM_LEDS];

uint8_t smooth[NUM_LEDS];
uint8_t luma[NUM_LEDS];

uint8_t buff[sizeof(leds_new) * 2 + 1];
unsigned buff_len = sizeof(buff);

unsigned smoothTime = 1000 / smoothFPS;
unsigned smoothIndex = 0xFF / smoothStep;

void led_flash() {
	int i;
	for (i = 0; i < 250; i += 10) {
		FastLED.showColor(CRGB(i, i, i));
		delay(35);
	}
	for (; i >= 0; i -= 10) {
		FastLED.showColor(CRGB(i, i, i));
		delay(35);
	}
}

void smooth_led(){
	int i = 0;
	while (i < NUM_LEDS) {
		if (leds[i] != leds_new[i]) {
			uint8_t luma_curr = leds[i].getAverageLight();//getLuma();
			uint8_t luma_new = leds_new[i].getAverageLight();//getLuma();
			uint8_t delta = abs(luma[i] - luma_new);
			if (!smooth[i] || delta > deltaMax) {
				delta /= smoothIndex;
				smooth[i] = (smoothStep > delta)? (smoothStep - delta)/2 : 1;
				luma[i] = luma_new;
			}
			uint8_t ind = smoothIndex * smooth[i];
			leds[i].r = lerp8by8(leds[i].r, leds_new[i].r, ind);
			leds[i].g = lerp8by8(leds[i].g, leds_new[i].g, ind);
			leds[i].b = lerp8by8(leds[i].b, leds_new[i].b, ind);

			if ((++smooth[i]) > smoothStep || abs(luma_curr - luma_new) <= deltaMin) {
				leds[i] = leds_new[i];
				luma[i] = luma_new;
				smooth[i] = 0;
			}
		}
		++i;
	}

	FastLED.show();
}

unsigned read_header(uint8_t *buff, unsigned size, unsigned *state, unsigned *pos){
	if (*state == _HEAD_OK) return PACKET_SIZE;

	while (*pos < size) {
		if (*state < _HEAD_HI) {
			if (buff[*pos] == header[*state]) (*state)++;
			else *state = _HEAD_1;
		} else
		if (*state < _HEAD_PKG) {
			header[*state] = buff[*pos];
			(*state)++;
		}
		if (*state == _HEAD_PKG) {
			if (header[_HEAD_CHK] == (header[_HEAD_HI] ^ header[_HEAD_LO] ^ 0x55)) {
				(*state)++;
				(*pos)++;
				return PACKET_SIZE;
			}
			*state = _HEAD_1;
		}
		(*pos)++;
	}
	return 0;
}

int read_serial(uint8_t reset){
	static unsigned l, state = 0, pos = 0, len = 0, dsize = 0;
	if (reset) {
		state = pos = len = dsize = 0;
		memset(buff, 0, buff_len);
		return 0;
	}

	if ((l = Serial.available())) {
		unsigned sz = buff_len - len;
		len += Serial.readBytes(buff + len, (l < sz)? l : sz);
		if ((dsize = read_header(buff, len, &state, &pos))) {
			if (len - pos >= dsize) {
				memcpy((uint8_t*)leds_new, buff + pos, dsize);
				pos += dsize;
				state = _HEAD_1;
			}
			if (pos) {
				memmove(buff, buff + pos, (len -= pos));
				pos = 0;
			}
			if (state == _HEAD_1) return 1;
		}
		if (pos >= buff_len - 1) {
			return read_serial(1);
		}
	}
	return 0;
}

void setup(){
	// инициализация светодиодов
	FastLED.addLeds<LED_TYPE, LED_PIN, LED_COLOR>(leds, NUM_LEDS);
	// FastLED.setBrightness(BRIGHTNESS);
	// FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);
	read_serial(1);

	memset(leds, 0, sizeof(leds));
	memset(leds_new, 0, sizeof(leds_new));
	memset(smooth, 0, sizeof(smooth));

	if (Serial.availableForWrite()) {
		Serial.end();
	}

	unsigned long detectedBaudrate = 0;//Serial.detectBaudrate(10000);
	Serial.begin(detectedBaudrate? detectedBaudrate : serialRate);
	Serial.setTimeout(1000);
	if (Serial.availableForWrite()) {
		Serial.print(ack);
	}

	// WiFi.forceSleepBegin();
	WiFi.preinitWiFiOff();
	led_flash();
}

void loop() {
	static unsigned long tm_packet = 0, tm_smooth = 0;
	unsigned long tm = millis();

	if (read_serial(0)) {
		tm_packet = millis();
	} else
	if (serialTimeout && tm_packet && (tm - tm_packet > serialTimeout * 60 * 1000)) {
		FastLED.clear();
		tm_packet = 0;
	}

	if (tm_packet && tm - tm_smooth > smoothTime) {
		tm_smooth = tm;
		smooth_led();
	}
}
