#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <wiringPi.h>
#include <qpointingdevice.h>

#include <QtGui/private/qhighdpiscaling_p.h>
#include <QtGui/qpa/qwindowsysteminterface.h>
#include <QWindow>

/* Front Panel controls */
char pins[15] = {0, 2, 3, 6, 7,
								10, 11, 12, 13, 14,
								21, 22, 23, 25, 27};

// the little knob
#define ENC1_A (13)
#define ENC1_B (12)
#define ENC1_SW (14)

// the big tuning knob
#define ENC2_A (0)
#define ENC2_B (2)
#define ENC2_SW (3)

#define SW5 (22)
#define PTT (7)
#define DASH (21)

#define ENC_FAST 1
#define ENC_SLOW 5

// https://morsecode.world/international/timing.html
#define MORSE_WPM 12
#define MORSE_DIT_MS MORSE_WPM 60 / (50 * MORSE_WPM)
// i.e. dit time is 0.1 sec

// encoder state
struct encoder {
	int pin_a,  pin_b;
	int speed;
	int prev_state;
	int history;
};
void enc_isr(void);

struct encoder enc_a, enc_b;

QPointingDevice *encoder = nullptr;
QPointingDevice *tuningEncoder = nullptr;

void init_gpio_pins(){
	for (int i = 0; i < 15; i++){
		pinMode(pins[i], INPUT);
		pullUpDnControl(pins[i], PUD_UP);
	}
}

void enc_init(struct encoder *e, int speed, int pin_a, int pin_b){
	e->pin_a = pin_a;
	e->pin_b = pin_b;
	e->speed = speed;
	e->history = 5;
}

int enc_state (struct encoder *e) {
	//~ printf("read %d : %d ; %d : %d\n", e->pin_a, digitalRead(e->pin_a), e->pin_b, digitalRead(e->pin_b));
	return (digitalRead(e->pin_a) ? 1 : 0) + (digitalRead(e->pin_b) ? 2: 0);
}

int enc_read(struct encoder *e) {
  int result = 0;
  int newState;

  newState = enc_state(e); // Get current state

  if (newState != e->prev_state)
     delay (1);

  if (enc_state(e) != newState || newState == e->prev_state)
    return 0;

  //these transitions point to the encoder being rotated anti-clockwise
  if ((e->prev_state == 0 && newState == 2) ||
    (e->prev_state == 2 && newState == 3) ||
    (e->prev_state == 3 && newState == 1) ||
    (e->prev_state == 1 && newState == 0)){
      e->history--;
      //result = -1;
    }
  //these transitions point to the encoder being rotated clockwise
  if ((e->prev_state == 0 && newState == 1) ||
    (e->prev_state == 1 && newState == 3) ||
    (e->prev_state == 3 && newState == 2) ||
    (e->prev_state == 2 && newState == 0)){
      e->history++;
    }
  e->prev_state = newState; // Record state for next pulse interpretation
  if (e->history > e->speed){
    result = 1;
    e->history = 0;
  }
  if (e->history < -e->speed){
    result = -1;
    e->history = 0;
  }
  return result;
}

static int enc_ticks = 0;
static int tuning_ticks = 0;

static QWindow *encoderWindow = nullptr;

void setEncoderWindow(QWindow *win) {
    encoderWindow = win;
}

void enc_isr(void){
	int val = enc_read(&enc_a);
    if (val) {
        const int ticks = enc_ticks;
        if (val < 0)
            enc_ticks++;
        if (val > 0)
            enc_ticks--;
        if (enc_ticks != ticks) {
            qint64 ts = QDateTime::currentMSecsSinceEpoch();
            QPoint gpos = QHighDpiScaling::mapPositionToNative(QCursor::pos(), encoderWindow->screen()->handle());
            QPoint wpos = QHighDpiScaling::mapPositionToNative(encoderWindow->mapFromGlobal(QCursor::pos()),
                                                               encoderWindow->screen()->handle());
            qDebug() << ts << "wheel" << (enc_ticks - ticks) << "@" << wpos << gpos;
            QWindowSystemInterface::handleWheelEvent(encoderWindow, ts, /* encoder, */ wpos, gpos, {},
                                                     {0, (enc_ticks - ticks) * 120});
        }
    }
	//~ printf("enc a %d\n", val);
	val = enc_read(&enc_b);
	if (val < 0)
		tuning_ticks--;
	if (val > 0)
		tuning_ticks++;
    //~ printf("enc b %d tuning %d\n", enc_ticks, tuning_ticks);
}

void key_timer_isr(int sig) {
	qint64 ts = QDateTime::currentMSecsSinceEpoch();
	// both are active-low inputs
	static bool pttState = true;
	static bool dashState = true;
	bool ptt = digitalRead(PTT);
	bool dash = digitalRead(DASH);
	//~ printf("%lld key state %d %d\n", ts, ptt, dash);
	if (ptt != pttState) {
		qDebug() << ts << "dit" << !ptt << encoderWindow;
		QWindowSystemInterface::handleKeyEvent(encoderWindow, //QDateTime::currentMSecsSinceEpoch(),
			(ptt ? QEvent::KeyRelease : QEvent::KeyPress), Qt::Key_Period, Qt::NoModifier); // , Qt::ControlModifier);
	} else if (dash != dashState) {
		qDebug() << ts << "dah" << !dash << encoderWindow;
		QWindowSystemInterface::handleKeyEvent(encoderWindow, //QDateTime::currentMSecsSinceEpoch(),
			(dash ? QEvent::KeyRelease : QEvent::KeyPress), Qt::Key_hyphen, Qt::NoModifier); //, Qt::ControlModifier);
	}
	pttState = ptt;
	dashState = dash;
}

void key_isr(void) {
	//~ qint64 ts = QDateTime::currentMSecsSinceEpoch();
	//~ printf("%lld key state %d %d\n", ts, digitalRead(PTT), digitalRead(DASH));
	ualarm(5000, 0); // debounce
}

void encodersInit() {
	wiringPiSetup();
	init_gpio_pins();

	enc_init(&enc_a, ENC_FAST, ENC1_A, ENC1_B);
	enc_init(&enc_b, ENC_FAST, ENC2_A, ENC2_B);

	wiringPiISR(ENC1_A, INT_EDGE_BOTH, enc_isr);
	wiringPiISR(ENC1_B, INT_EDGE_BOTH, enc_isr);
	wiringPiISR(ENC2_A, INT_EDGE_BOTH, enc_isr);
	wiringPiISR(ENC2_B, INT_EDGE_BOTH, enc_isr);
	wiringPiISR(PTT, INT_EDGE_BOTH, key_isr);
	wiringPiISR(DASH, INT_EDGE_BOTH, key_isr);
	signal(SIGALRM, key_timer_isr);

    encoder = new QPointingDevice("little knob", 72ll,
                                  QPointingDevice::DeviceType::Unknown, QPointingDevice::PointerType::Generic,
                                  QInputDevice::Capability::Scroll, 0, 1);
    tuningEncoder = new QPointingDevice("tuning knob", 73ll,
                                        QInputDevice::DeviceType::Unknown, QPointingDevice::PointerType::Generic,
                                        QInputDevice::Capability::Scroll, 0, 1);
    QWindowSystemInterface::registerInputDevice(encoder);
    QWindowSystemInterface::registerInputDevice(tuningEncoder);
}

static time_t buttonPressTime;
static int buttonPressed = 0;

void handleButtonPress() {
    static int menuVisible = 0;
    static time_t buttonPressTime = 0;
    static int buttonPressed = 0;

    if (digitalRead(ENC1_SW) == 0) {
        if (!buttonPressed) {
            buttonPressed = 1;
            buttonPressTime = time(NULL);
        } else {
            // Check the duration of the button press
            time_t currentTime = time(NULL);
            if (difftime(currentTime, buttonPressTime) >= 1) {
                // Long press detected
                menuVisible = !menuVisible;
                // Wait for the button release to avoid immediate short press detection
                while (digitalRead(ENC1_SW) == 0) {
                    delay(100); // Adjust delay time as needed
                }
                buttonPressed = 0; // Reset button press state after delay
            }
        }
    } else {
        if (buttonPressed) {
            buttonPressed = 0;
            if (difftime(time(NULL), buttonPressTime) < 1) {
                // Short press detected
            }
         }
      }
   }
