// ============================================================
//  SpaceOS ESP32 v1.0 - HW Electrics
//  NodeMCU ESP32
//  Libs: LiquidCrystal_I2C, Keypad, RTClib, Wire, Preferences, SD
// ============================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <RTClib.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>

// ── Pins ─────────────────────────────────────────────────────
#define PIN_GREEN   2
#define PIN_WHITE   4
#define PIN_BUZZ    5
#define PIN_BTN    15
#define PIN_SD_CS  16

// ── Lautsprecher (ESP32 LEDC) ────────────────────────────────

void buzzTone(int hz, int ms) {
  if (hz > 0) {
    ledcAttach(PIN_BUZZ, hz, 8);
    ledcWrite(PIN_BUZZ, 128);
  }
  delay(ms);
  ledcWrite(PIN_BUZZ, 0);
  ledcDetach(PIN_BUZZ);
}

// ── LCD I2C ──────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── RTC ──────────────────────────────────────────────────────
RTC_DS1307 rtc;
bool rtcOK = false;

// ── Keypad ───────────────────────────────────────────────────
const byte KR = 4, KC = 4;
char keys[KR][KC] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[KR] = {13,12,14,27};
byte colPins[KC] = {26,25,33,32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, KR, KC);

// ── Preferences (ersetzt EEPROM) ─────────────────────────────
Preferences prefs;

// ── SD ───────────────────────────────────────────────────────
bool sdOK = false;

// ── WiFi ─────────────────────────────────────────────────────
char wifiSSID[33] = "";
char wifiPASS[65] = "";
bool wifiON = false;
#define WIFI_MAX_NETS 8
String wifiNets[WIFI_MAX_NETS];
int wifiNetCount = 0;
int wifiScanSel  = 0;

// ── ChatGPT Key (früh deklariert wegen pWipe) ────────────────
String chatGPTKey = "";


// ── T9 ───────────────────────────────────────────────────────
const char TM0[] = ".,!? ";
const char TM1[] = "ABC";
const char TM2[] = "DEF";
const char TM3[] = "GHI";
const char TM4[] = "JKL";
const char TM5[] = "MNO";
const char TM6[] = "PQRS";
const char TM7[] = "TUV";
const char TM8[] = "WXYZ";
const char TM9[] = " 0";
const char* TM[10] = {TM0,TM1,TM2,TM3,TM4,TM5,TM6,TM7,TM8,TM9};
#define T9TMO 900UL

char          tBuf[5][64];
int           tLen[5];
char          tPK[5];
int           tPI[5];
unsigned long tPT[5];
bool          tNum[5];
int           tMode[5];   // 0=Gross 1=Klein 2=Zahlen

void tReset(int id, bool num) {
  memset(tBuf[id],0,64);
  tLen[id]=0; tPK[id]=0; tPI[id]=0; tPT[id]=0; tNum[id]=num; tMode[id]=0;
}

void tCommit(int id) {
  if (!tPK[id]) return;
  if (tLen[id]>=63) { tPK[id]=0; tPI[id]=0; return; }
  // Im Zahlenmodus: Taste direkt als Ziffer
  if(tMode[id]==2 || tNum[id]) {
    tBuf[id][tLen[id]++]=tPK[id];
    tBuf[id][tLen[id]]=0;
    tPK[id]=0; tPI[id]=0;
    return;
  }
  int k=(tPK[id]=='0')?9:(tPK[id]-'1');
  int sl=strlen(TM[k]);
  char c=TM[k][tPI[id]%sl];
  if(tMode[id]==1 && c>='A' && c<='Z') c=c+32;
  tBuf[id][tLen[id]++]=c;
  tBuf[id][tLen[id]]=0;
  tPK[id]=0; tPI[id]=0;
}

void tKey(int id, char key) {
  // A/B/C/D sind Steuertasten, nie als Eingabe
  if(key=='A'||key=='B'||key=='C'||key=='D') return;
  if (key>='0'&&key<='9') {
    // Im Zahlen-Modus (tMode==2 oder tNum): direkt Ziffer einfügen
    if (tNum[id] || tMode[id]==2) {
      if (tLen[id]<63) { tBuf[id][tLen[id]++]=key; tBuf[id][tLen[id]]=0; }
      return;
    }
    if (key==tPK[id]) { tPI[id]++; tPT[id]=millis(); }
    else { tCommit(id); tPK[id]=key; tPI[id]=0; tPT[id]=millis(); }
    return;
  }
  if (key=='*') {
    tCommit(id);
    if (tLen[id]>0) { tLen[id]--; tBuf[id][tLen[id]]=0; }
  }
  if (key=='#' && !tNum[id]) {
    tCommit(id);
    tMode[id]=(tMode[id]+1)%3;  // 0=Gross -> 1=Klein -> 2=Zahlen -> 0=Gross
  }
}

bool tTick(int id) {
  if (tPK[id] && (millis()-tPT[id])>T9TMO) { tCommit(id); return true; }
  return false;
}

void tDisp(int id, char* out) {
  int i;
  for(i=0;i<tLen[id]&&i<63;i++) out[i]=tBuf[id][i];
  if (tPK[id]) {
    int k=(tPK[id]=='0')?9:(tPK[id]-'1');
    out[i++]=TM[k][tPI[id]%(int)strlen(TM[k])];
  }
  out[i]=0;
}

void tGet(int id, char* out) { tCommit(id); strcpy(out,tBuf[id]); }

// ── Zustände ─────────────────────────────────────────────────
#define ST_SETUP_NAME    0
#define ST_SETUP_PASS    1
#define ST_SETUP_DATE    2
#define ST_SETUP_TIME    3
#define ST_LOGIN_NAME    4
#define ST_LOGIN_PASS    5
#define ST_DESKTOP       6
#define ST_APPMENU       7
#define ST_SHUTMENU      8
#define ST_NOTES         9
#define ST_NOTESMENU    10
#define ST_NOTESOPEN    11
#define ST_CLOCK        12
#define ST_CALC         13
#define ST_DIAGMENU     14
#define ST_DIAGRESET    15
#define ST_FORGOT_N     16
#define ST_FORGOT_P     17
#define ST_SETTINGS     18
#define ST_SET_DATE     19
#define ST_SET_TIME     20
#define ST_SET_CHPW_OLD 21
#define ST_SET_CHPW_NEW 22
#define ST_SET_CHUN_OLD 23
#define ST_SET_CHUN_NEW 24
#define ST_SET_RESET    25
#define ST_SET_VOL      26
#define ST_SNAKE        27
#define ST_SNAKE_SCORE  28
#define ST_SNAKE_NAME   29
#define ST_DIAG_PW      30
#define ST_SAVE_WHERE   31
#define ST_WIFI_SCAN    32
#define ST_WIFI_PASS    33
#define ST_WIFI_INFO    34
#define ST_BT_MENU      35
#define ST_OTA_MODE     36
#define ST_CHATGPT      37
#define ST_API_KEY      38
#define ST_LANG_SEL     39
#define ST_STOPWATCH    40
#define ST_WEATHER      41
#define ST_TIMER        42
#define ST_WIFI_SSID    43

int S = ST_LOGIN_NAME;

// ── Benutzerdaten ────────────────────────────────────────────
char gU[17] = "";
char gP[7]  = "";
int  gVol   = 2;

// ── Sprache / Language ───────────────────────────────────────
int gLang = 0; // 0=Deutsch 1=English

// Alle UI-Strings in beiden Sprachen
// Zugriff: T(DE, EN)
#define T(de,en) (gLang==0?(de):(en))


// ── LCD Helfer ───────────────────────────────────────────────
void lRow(int row) {
  lcd.setCursor(0,row);
  lcd.print("                ");
  lcd.setCursor(0,row);
}

void lSet(const char* a, const char* b) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(a);
  lcd.setCursor(0,1); lcd.print(b);
}

void lMid(int row, const char* txt) {
  int n=strlen(txt), c=(16-n)/2;
  if(c<0)c=0;
  lRow(row); lcd.setCursor(c,row); lcd.print(txt);
}

void lScroll(int row, const char* txt, int reps) {
  int n=strlen(txt);
  if(n<=16) { lRow(row); lcd.setCursor(0,row); lcd.print(txt); return; }
  for(int r=0;r<reps;r++) {
    for(int i=0;i<=n-16;i++) {
      lRow(row); lcd.setCursor(0,row);
      for(int j=0;j<16;j++) lcd.print(txt[i+j]);
      delay(280);
      if(keypad.getKey()) return;
    }
    delay(400);
  }
}

void showField(int row, int id) {
  char d[65]; tDisp(id,d);
  int n=strlen(d);
  // Zeige letzte 13 Zeichen wenn zu lang (scrollt automatisch)
  int start=0;
  if(n>13) start=n-13;
  lRow(row);
  for(int i=start;i<n;i++) lcd.print(d[i]);
  lcd.print('_');
  // Modus Anzeige ganz rechts
  lcd.setCursor(15,row);
  if(tMode[id]==0) lcd.print('A');
  else if(tMode[id]==1) lcd.print('a');
  else lcd.print('1');
}

void showStars(int row, int cnt) {
  lRow(row);
  for(int i=0;i<cnt;i++) lcd.print('*');
  lcd.print('_');
}

// ── Sound ────────────────────────────────────────────────────
void bip(int hz, int ms) {
  if(gVol==0) return;
  if(gVol==1) { buzzTone(hz, ms/2); delay(ms/2); }
  else buzzTone(hz, ms);
}

void sndBoot()     { bip(523,80);delay(40);bip(659,80);delay(40);bip(784,150); }
void sndOk()       { bip(659,60);delay(20);bip(880,100); }
void sndErr()      { bip(440,80);delay(20);bip(330,80);delay(20);bip(220,150); }
void sndClick()    { if(gVol>0) buzzTone(1047,15); }
void sndSave()     { bip(784,60);delay(20);bip(1047,100); }
void sndShutdown() { bip(523,80);delay(30);bip(392,120); }
void sndGameOver() { bip(440,80);delay(20);bip(330,80);delay(20);bip(220,200); }
void sndPoint()    { if(gVol>0) buzzTone(880,25); }
void sndTimerEnd() { bip(880,150);delay(80);bip(880,150);delay(80);bip(880,150); }

// ── Zeit / Datum ─────────────────────────────────────────────
void tStr(char* b) {
  if(!rtcOK) { strcpy(b,"--:--:--"); return; }
  DateTime n=rtc.now();
  sprintf(b,"%02d:%02d:%02d",n.hour(),n.minute(),n.second());
}

void dStr(char* b) {
  if(!rtcOK) { strcpy(b,"--.--.----"); return; }
  DateTime n=rtc.now();
  sprintf(b,"%02d.%02d.%04d",n.day(),n.month(),n.year());
}

void dStrShort(char* b) {
  if(!rtcOK) { strcpy(b,"--.--.--"); return; }
  DateTime n=rtc.now();
  sprintf(b,"%02d.%02d.%02d",n.day(),n.month(),n.year()%100);
}

// ── Preferences Helfer ───────────────────────────────────────
void pSave() {
  prefs.begin("spaceos", false);
  prefs.putString("user", gU);
  prefs.putString("pass", gP);
  prefs.putInt("vol", gVol);
  prefs.putInt("lang", gLang);
  prefs.putBool("setup", true);
  prefs.end();
}

void pLoad() {
  prefs.begin("spaceos", true);
  String u = prefs.getString("user","");
  String p = prefs.getString("pass","");
  gVol     = prefs.getInt("vol",2);
  gLang    = prefs.getInt("lang",0);
  prefs.end();
  strncpy(gU, u.c_str(), 16); gU[16]=0;
  strncpy(gP, p.c_str(),  6); gP[6]=0;
}

bool pSetupDone() {
  prefs.begin("spaceos", true);
  bool done = prefs.getBool("setup", false);
  prefs.end();
  return done;
}

void pWipe() {
  // Alle Namespaces löschen
  prefs.begin("spaceos", false); prefs.clear(); prefs.end();
  prefs.begin("wifi",    false); prefs.clear(); prefs.end();
  prefs.begin("notes",   false); prefs.clear(); prefs.end();
  prefs.begin("snake",   false); prefs.clear(); prefs.end();
  prefs.begin("gpt",     false); prefs.clear(); prefs.end();
  // Variablen zurücksetzen
  gVol=2; gLang=0;
  memset(wifiSSID,0,33); memset(wifiPASS,0,65);
  chatGPTKey="";
}

int pForceCount() {
  prefs.begin("spaceos", true);
  int fc = prefs.getInt("fcnt", 0);
  prefs.end();
  return fc;
}

void pSetForce(int v) {
  prefs.begin("spaceos", false);
  prefs.putInt("fcnt", v);
  prefs.end();
}

void pResetForce() { pSetForce(0); }

// ── Notizen (in Preferences gespeichert) ─────────────────────
#define NL 30
#define NW 14
char nLines[NL][NW+1];
int  nTot=1, nView=0, nCol=0;

void nClear() {
  for(int i=0;i<NL;i++) memset(nLines[i],0,NW+1);
  nTot=1; nView=0; nCol=0;
}

void nAdd(char c) {
  int cur=nTot-1;
  if(nCol<NW) { nLines[cur][nCol++]=c; nLines[cur][nCol]=0; }
  else if(nTot<NL) {
    nTot++; cur=nTot-1;
    memset(nLines[cur],0,NW+1);
    nLines[cur][0]=c; nCol=1;
    if(nView<nTot-2) nView=nTot-2;
  }
}

void nFlush() {
  tCommit(2);
  for(int i=0;i<tLen[2];i++) nAdd(tBuf[2][i]);
  tReset(2,false);
}

void nShow() {
  lcd.clear();
  for(int row=0;row<2;row++) {
    int li=nView+row; if(li>=nTot) break;
    lcd.setCursor(0,row);
    if(li==nTot-1) {
      for(int i=0;i<nCol&&i<NW;i++) lcd.print(nLines[li][i]);
      char d[17]; tDisp(2,d);
      int dl=strlen(d), room=NW-nCol;
      for(int i=0;i<dl&&i<room;i++) lcd.print(d[i]);
      int cp=nCol+dl; if(cp<15){lcd.setCursor(cp,row);lcd.print('_');}
    } else {
      lcd.print(nLines[li]);
    }
  }
}

int nCount() {
  prefs.begin("notes", true);
  int cnt = prefs.getInt("cnt", 0);
  prefs.end();
  return cnt;
}

void nSave(int idx) {
  prefs.begin("notes", false);
  String key = "n" + String(idx);
  String txt = "";
  for(int i=0;i<nTot;i++) { txt += String(nLines[i]); txt += "\n"; }
  prefs.putString(key.c_str(), txt);
  prefs.putInt("cnt", max(nCount(), idx+1));
  prefs.end();
}

void nLoad(int idx) {
  prefs.begin("notes", true);
  String key = "n" + String(idx);
  String txt = prefs.getString(key.c_str(), "");
  prefs.end();
  nClear();
  int line=0, col=0;
  for(int i=0;i<(int)txt.length()&&line<NL;i++) {
    char c=txt[i];
    if(c=='\n') { line++; col=0; nTot=line+1; }
    else if(col<NW) { nLines[line][col++]=c; nLines[line][col]=0; }
  }
  if(nTot<1) nTot=1;
  nView=0; nCol=strlen(nLines[nTot-1]);
}

void nSaveSD(int idx) {
  if(!sdOK) return;
  if(!SD.exists("/Notizen")) SD.mkdir("/Notizen");
  char fname[32]; sprintf(fname,"/Notizen/Text%d.txt",idx+1);
  File f=SD.open(fname, FILE_WRITE);
  if(f) {
    for(int i=0;i<nTot;i++) { f.println(nLines[i]); }
    f.close();
  }
}

// ── Snake ────────────────────────────────────────────────────
#define SNAKE_MAX 25
int snX[SNAKE_MAX], snY[SNAKE_MAX];
int snLen=0, snDir=0, snFx=0, snFy=0, snScore=0;
unsigned long snT=0;
#define SN_SPEED 500UL

void snRandFood() {
  int tries=0;
  while(tries<50) {
    snFx=random(16); snFy=random(2);
    bool ok=true;
    for(int i=0;i<snLen;i++) if(snX[i]==snFx&&snY[i]==snFy){ok=false;break;}
    if(ok) return;
    tries++;
  }
}

void snDraw() {
  lcd.clear();
  char sc[5]; sprintf(sc,"%4d",snScore);
  lcd.setCursor(12,1); lcd.print(sc);
  lcd.setCursor(snFx,snFy); lcd.print('*');
  for(int i=0;i<snLen;i++) {
    if(!(snY[i]==1&&snX[i]>=12))
      { lcd.setCursor(snX[i],snY[i]); lcd.print(i==0?'O':'o'); }
  }
}

void snStart() {
  snLen=3; snDir=0; snScore=0;
  snX[0]=4; snY[0]=0;
  snX[1]=3; snY[1]=0;
  snX[2]=2; snY[2]=0;
  lcd.clear(); snRandFood(); snT=millis(); snDraw();
}

bool snMove() {
  int nx=snX[0], ny=snY[0];
  if(snDir==0)nx++; else if(snDir==1)ny++;
  else if(snDir==2)nx--; else ny--;
  if(nx<0)nx=15; if(nx>15)nx=0;
  if(ny<0)ny=1;  if(ny>1)ny=0;
  if(ny==1&&nx>=12) return true;
  for(int i=0;i<snLen;i++) if(snX[i]==nx&&snY[i]==ny) return true;
  bool ate=(nx==snFx&&ny==snFy);
  if(!ate) {
    for(int i=snLen-1;i>0;i--){snX[i]=snX[i-1];snY[i]=snY[i-1];}
    snX[0]=nx; snY[0]=ny;
  } else {
    if(snLen<SNAKE_MAX) {
      for(int i=snLen;i>0;i--){snX[i]=snX[i-1];snY[i]=snY[i-1];}
      snX[0]=nx; snY[0]=ny; snLen++;
    }
    snScore+=10; sndPoint(); snRandFood();
  }
  return false;
}

void snSaveScore(const char* nm) {
  prefs.begin("snake", false);
  // Top 3 lesen
  char names[3][17]; int scores[3]; char dates[3][11];
  for(int i=0;i<3;i++) {
    String k="n"+String(i);
    String ks="s"+String(i);
    String kd="d"+String(i);
    String ns=prefs.getString(k.c_str(),"");
    strncpy(names[i],ns.c_str(),16); names[i][16]=0;
    scores[i]=prefs.getInt(ks.c_str(),0);
    String ds=prefs.getString(kd.c_str(),"");
    strncpy(dates[i],ds.c_str(),10); dates[i][10]=0;
  }
  // Einsetzen
  int pos=-1;
  for(int i=0;i<3;i++) if(snScore>scores[i]){pos=i;break;}
  if(pos>=0) {
    for(int i=2;i>pos;i--){strcpy(names[i],names[i-1]);scores[i]=scores[i-1];strcpy(dates[i],dates[i-1]);}
    strcpy(names[pos],nm); scores[pos]=snScore; dStrShort(dates[pos]);
    for(int i=0;i<3;i++){
      prefs.putString(("n"+String(i)).c_str(),names[i]);
      prefs.putInt(("s"+String(i)).c_str(),scores[i]);
      prefs.putString(("d"+String(i)).c_str(),dates[i]);
    }
  }
  prefs.end();
}

void goSnakeScore() {
  S=ST_SNAKE_SCORE; lcd.clear();
  prefs.begin("snake",true);
  for(int i=0;i<2;i++) {
    String n=prefs.getString(("n"+String(i)).c_str(),"---");
    int sc=prefs.getInt(("s"+String(i)).c_str(),0);
    char buf[17]; sprintf(buf,"%d.%-8s%4d",i+1,n.c_str(),sc);
    lcd.setCursor(0,i);
    for(int j=0;j<16&&buf[j];j++) lcd.print(buf[j]);
  }
  prefs.end();
}

// ── Menü Variablen ───────────────────────────────────────────
int appSel=0, shutSel=0, nmSel=0, noSel=0, diagSel=0, setSel=0;

// ── Rechner ──────────────────────────────────────────────────
long cA=0,cB=0; char cOp=0,cBuf[16]="0"; int cBLen=1,cHas=0;


// ── Stoppuhr ─────────────────────────────────────────────────
unsigned long swStart = 0;
unsigned long swElapsed = 0;
bool swRunning = false;

// ── Countdown-Timer ──────────────────────────────────────────
long          tmTotalSec   = 0;     // eingestellte Gesamtzeit in Sekunden
long          tmRemainSec  = 0;     // verbleibende Sekunden
unsigned long tmLastTick   = 0;     // letzter Sekunden-Tick
bool          tmRunning    = false;
bool          tmFinished   = false; // Timer ist abgelaufen, wartet auf Tastendruck
unsigned long tmBlinkT      = 0;     // Blink-Timer fürs Alarm-Blinken
bool          tmBlinkOn     = false;

// ── Timer ────────────────────────────────────────────────────
unsigned long deskT=0, clkT=0;

// ── Button ───────────────────────────────────────────────────
unsigned long btnT=0; bool btnH=false,btn3=false;

// ── D Langdruck ──────────────────────────────────────────────
unsigned long dLT=0; bool dLH=false,dLDn=false;

// ── Sonstiges ────────────────────────────────────────────────
char forgN[17]="";

// ── Draw Menüs ───────────────────────────────────────────────
void drawApp() {
  const char* it[8]={T("Notizen","Notes"),"Uhr","Rechner","Einstellungen","Snake","ChatGPT","Stoppuhr",T("Timer","Timer")};
  lcd.clear();
  lcd.setCursor(0,0); lcd.print('>'); lcd.print(it[appSel]);
  lcd.setCursor(0,1); lcd.print(' '); lcd.print(it[(appSel+1)%8]);
}

void drawShut() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(shutSel==0?'>':' '); lcd.print(T("Ausschalten","Shut down"));
  lcd.setCursor(0,1); lcd.print(shutSel==1?'>':' '); lcd.print(T("Neustart","Restart"));
}

void drawNM() {
  const char* it[3]={"Speichern","Oeffnen","Beenden"};
  lcd.clear();
  lcd.setCursor(0,0); lcd.print('>'); lcd.print(it[nmSel]);
  lcd.setCursor(0,1); lcd.print(' '); lcd.print(it[(nmSel+1)%3]);
}

void drawNO() {
  int cnt=nCount(); lcd.clear(); char b[17];
  sprintf(b,">Text%d.txt",noSel+1); lcd.setCursor(0,0); lcd.print(b);
  if(cnt>1){sprintf(b," Text%d.txt",(noSel+1)%cnt+1);lcd.setCursor(0,1);lcd.print(b);}
}

void drawMenuLine(int row, const char* txt, bool sel) {
  lcd.setCursor(0,row); lcd.print(sel?'>':' ');
  int n=strlen(txt);
  if(n<=14){lcd.print(txt);return;}
  // Laufschrift fuer lange Eintraege
  for(int i=0;i<=n-14;i++){
    lcd.setCursor(1,row);
    for(int j=0;j<14;j++) lcd.print(txt[i+j]);
    delay(280);
    char k=keypad.getKey();
    if(k=='A'||k=='B'||k=='D'||k=='C') return;
  }
}

void drawDiag() {
  const char* it[3]={T("SpaceOS starten","Start SpaceOS"),T("Passwort vergessen","Forgot password"),T("Zuruecksetzen...","Factory reset...")};
  lcd.clear();
  drawMenuLine(0,it[diagSel],true);
  drawMenuLine(1,it[(diagSel+1)%3],false);
}

#define SET_COUNT 11
void drawSet() {
  const char* itDE[SET_COUNT]={"Datum & Uhrzeit","Passwort aendern","Benutzer aendern","Auto-Diagnose","Zuruecksetzen","Lautstaerke","WLAN","OTA Update","ChatGPT API Key","Sprache","Zurueck"};
  const char* itEN[SET_COUNT]={"Date & Time","Change Password","Change Username","Auto-Diagnostics","Factory Reset","Volume","WiFi","OTA Update","ChatGPT API Key","Language","Back"};
  const char** it = gLang==0 ? itDE : itEN;
  lcd.clear();
  drawMenuLine(0,it[setSel],true);
  drawMenuLine(1,it[(setSel+1)%SET_COUNT],false);
}

void drawDateIn() {
  char* b=tBuf[0]; int n=tLen[0]; char o[11]; strcpy(o,"  .  .    ");
  int p[8]={0,1,3,4,6,7,8,9};
  for(int i=0;i<n&&i<8;i++) o[p[i]]=b[i];
  if(n<8) o[p[n]]='_'; o[10]=0;
  lRow(1); lcd.print(o);
}

void drawTimeIn() {
  char* b=tBuf[0]; int n=tLen[0]; char o[9]; strcpy(o,"__:__:__");
  int p[6]={0,1,3,4,6,7};
  for(int i=0;i<n&&i<6;i++) o[p[i]]=b[i];
  if(n<6) o[p[n]]='_'; o[8]=0;
  lRow(1); lcd.print(o);
}

// ── Screen Starter ───────────────────────────────────────────
void goDesk() {
  S=ST_DESKTOP; lcd.clear();
  char tb[9]; tStr(tb); lcd.setCursor(0,0); lcd.print(tb);
  lMid(1,T("Desktop","Desktop")); deskT=millis();
  pResetForce();
}

void goLoginN() { S=ST_LOGIN_NAME; tReset(1,false); lSet(T(T("Benutzername:","Username:"),"Username:"),""); showField(1,1); }
void goLoginP() { S=ST_LOGIN_PASS; tReset(1,true);  lSet(T(T("Passwort:","Password:"),"Password:"),""); showStars(1,0); }
void goLangSel() {
  S=ST_LANG_SEL;
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(gLang==0?">Deutsch":" Deutsch");
  lcd.setCursor(0,1); lcd.print(gLang==1?">English":" English");
}

void goSetupN() { S=ST_SETUP_NAME; tReset(0,false); lSet(T("Name:","Name:"),""); showField(1,0); }
void goSetupP() { S=ST_SETUP_PASS; tReset(0,true);  lSet(T(T("Passwort:","Password:"),"Password:"),""); showStars(1,0); }

void goSetupD() {
  S=ST_SETUP_DATE; tReset(0,true);
  lcd.clear(); lcd.setCursor(0,0); lcd.print(T("Datum:TT.MM.JJJJ","Date:DD.MM.YYYY"));
  lcd.setCursor(0,1); lcd.print("__.__.____");
}

void goSetupT() {
  S=ST_SETUP_TIME; tReset(0,true);
  lcd.clear(); lcd.setCursor(0,0); lcd.print(T("Zeit: HH:MM:SS","Time: HH:MM:SS"));
  lcd.setCursor(0,1); lcd.print("__:__:__");
}

void goAppMenu()  { S=ST_APPMENU;   appSel=0;  drawApp(); }
void goShutMenu() { S=ST_SHUTMENU;  shutSel=0; drawShut(); }
void goNoteMenu() { S=ST_NOTESMENU; nmSel=0;   drawNM(); }
void goDiagMenu() { S=ST_DIAGMENU;  diagSel=0; drawDiag(); }
void goSettings() { S=ST_SETTINGS;  setSel=0;  drawSet(); }

void goNoteOpen() {
  int cnt=nCount();
  if(!cnt){lSet(T("Keine Notizen","No notes"),T("gespeichert!","stored!"));sndErr();delay(1500);goNoteMenu();return;}
  S=ST_NOTESOPEN; noSel=0; drawNO();
}

void goNotes() {
  nClear(); tReset(2,false);
  lcd.clear(); lMid(0,T("Notizen","Notes")); delay(800);
  S=ST_NOTES; nShow();
}

void goClock() {
  S=ST_CLOCK; lMid(0,"Uhr"); lRow(1); delay(800);
  char t[9],d[11]; tStr(t); dStr(d);
  lcd.clear(); lRow(0); lcd.print(t); lRow(1); lcd.print(d);
  clkT=millis();
}

void goCalc() {
  S=ST_CALC; cA=0;cB=0;cOp=0;cHas=0; strcpy(cBuf,"0"); cBLen=1;
  lMid(0,"Rechner"); lRow(1); delay(800);
  lcd.clear(); lcd.setCursor(0,0); lcd.print("0");
  lcd.setCursor(0,1); lcd.print("A+ B- *x #/ D=");
}

void goSnake() {
  S=ST_SNAKE;
  lcd.clear(); lMid(0,"Snake"); lMid(1,"A/B/C/#=Richt."); delay(1500);
  snStart();
}


// ── Stoppuhr ─────────────────────────────────────────────────
void goStopwatch() {
  S=ST_STOPWATCH;
  swElapsed=0; swRunning=false; swStart=0;
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(T("Stoppuhr","Stopwatch"));
  lcd.setCursor(0,1); lcd.print("00:00.00");
  lRow(0); lcd.print(T(">A=Start C=Zurueck",">A=Start C=Back"));
}

void swDraw() {
  unsigned long ms = swRunning ? (millis()-swStart+swElapsed) : swElapsed;
  unsigned long h  = ms/3600000;
  unsigned long m  = (ms%3600000)/60000;
  unsigned long s  = (ms%60000)/1000;
  unsigned long cs = (ms%1000)/10;
  char buf[17];
  if(h>0) sprintf(buf,"%02lu:%02lu:%02lu.%02lu",h,m,s,cs);
  else     sprintf(buf,"%02lu:%02lu.%02lu",m,s,cs);
  lRow(0); lcd.print(buf);
  lcd.setCursor(0,1);
  if(swRunning) lcd.print(T("A=Stop  B=Runde ","A=Stop  B=Lap   "));
  else          lcd.print(T("A=Start *=Reset ","A=Start *=Reset "));
}

// ── Countdown-Timer ──────────────────────────────────────────
// Eingabe erfolgt als MMSS über T9-Puffer 0 (reiner Zahlenmodus)
void goTimer() {
  S=ST_TIMER;
  tmRunning=false; tmFinished=false; tmTotalSec=0; tmRemainSec=0;
  tmBlinkOn=false;
  tReset(0,true); // Zahlen-Puffer für die Eingabe MMSS
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(T("Timer: MM:SS","Timer: MM:SS"));
  lcd.setCursor(0,1); lcd.print("__:__");
}

void tmDrawInput() {
  char* b=tBuf[0]; int n=tLen[0];
  char o[6]; strcpy(o,"__:__");
  int p[4]={0,1,3,4};
  for(int i=0;i<n&&i<4;i++) o[p[i]]=b[i];
  lRow(1); lcd.print(o);
}

void tmDrawRun() {
  long m = tmRemainSec/60, s = tmRemainSec%60;
  char buf[9]; sprintf(buf,"%02ld:%02ld",m,s);
  lRow(0); lcd.print(buf);
  lcd.setCursor(0,1);
  lcd.print(tmRunning ? T("A=Pause *=Reset ","A=Pause *=Reset ")
                       : T("A=Start *=Reset ","A=Start *=Reset "));
}

void tmStartFromInput() {
  char* b=tBuf[0]; int n=tLen[0];
  if(n<4) return; // braucht volle MMSS
  int mm=(b[0]-'0')*10+(b[1]-'0');
  int ss=(b[2]-'0')*10+(b[3]-'0');
  long total=(long)mm*60+ss;
  if(total<=0) { sndErr(); return; }
  tmTotalSec=total; tmRemainSec=total;
  tmRunning=true; tmFinished=false;
  tmLastTick=millis();
  sndClick();
  lcd.clear();
  tmDrawRun();
}

void tmAlarmStep() {
  // Blinkt LED + piept solange bis Taste gedrückt wird
  if(millis()-tmBlinkT>400) {
    tmBlinkT=millis();
    tmBlinkOn=!tmBlinkOn;
    digitalWrite(PIN_WHITE, tmBlinkOn?HIGH:LOW);
    if(tmBlinkOn) sndTimerEnd();
  }
}

void tmStop() {
  tmRunning=false; tmFinished=false;
  digitalWrite(PIN_WHITE, LOW);
}

// ── Wetter ───────────────────────────────────────────────────
void goWeather() {
  S=ST_WEATHER;
  lcd.clear();
  if(!wifiON){
    lSet(T("Kein WLAN!","No WiFi!"),T("Erst WLAN setup","Setup WiFi first"));
    sndErr(); delay(2000); goAppMenu(); return;
  }
  lcd.setCursor(0,0); lcd.print(T("Wetter laden...","Loading weather."));
  lRow(1);
  HTTPClient http;
  // wttr.in gibt kompaktes Format zurück, kein API Key nötig
  http.begin("http://wttr.in/?format=%t+%C&lang=de");
  http.setTimeout(8000);
  int code2 = http.GET();
  if(code2==200){
    String resp = http.getString();
    resp.trim();
    // Zeile 0: Temperatur, Zeile 1: Wetterlage
    int sp = resp.indexOf(' ');
    String temp = sp>0 ? resp.substring(0,sp) : resp;
    String cond = sp>0 ? resp.substring(sp+1) : "";
    if(cond.length()>16) cond=cond.substring(0,16);
    lcd.clear();
    lRow(0); lcd.print(T("Wetter: ","Weather: ")); lcd.print(temp);
    lRow(1); lcd.print(cond);
  } else {
    lcd.clear();
    char errBuf[8]; sprintf(errBuf,"%d",code2);
    lSet(T("Fehler!","Error!"), errBuf);
    sndErr();
  }
  http.end();
}

// ── SD Update Check ──────────────────────────────────────────
void checkSDUpdate() {
  if(!sdOK) return;
  File dir=SD.open("/Updates");
  if(!dir) return;
  File f=dir.openNextFile();
  if(!f){dir.close();return;}
  String fname=f.name();
  f.close(); dir.close();

  lcd.clear();
  lMid(0,"Update gefunden!");
  lMid(1,T("D=Ja  C=Nein","D=Yes  C=No"));

  unsigned long wt=millis();
  while(millis()-wt<15000) {
    char k=keypad.getKey();
    if(k=='C') return;
    if(k=='D') {
      lcd.clear(); lMid(0,T("Installiere...","Installing..."));
      lcd.setCursor(0,1); lcd.print("[                ]");
      for(int i=0;i<16;i++) {
        lcd.setCursor(i+1,1); lcd.print('#');
        digitalWrite(PIN_WHITE,HIGH); delay(300);
        digitalWrite(PIN_WHITE,LOW);  delay(200);
      }
      lcd.clear(); lMid(0,"Update fertig!"); lMid(1,T("Neustart...","Restarting..."));
      sndOk(); delay(2000);
      SD.remove(("/Updates/"+fname).c_str());
      ESP.restart();
      return;
    }
  }
}


// ── WiFi Funktionen ──────────────────────────────────────────
void wifiLoad() {
  prefs.begin("wifi", true);
  String s=prefs.getString("ssid","");
  String p=prefs.getString("pass","");
  prefs.end();
  strncpy(wifiSSID,s.c_str(),32); wifiSSID[32]=0;
  strncpy(wifiPASS,p.c_str(),64); wifiPASS[64]=0;
}

void wifiSave() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("pass", wifiPASS);
  prefs.end();
}

bool wifiConnect() {
  if(strlen(wifiSSID)<1) return false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(300);
  WiFi.begin(wifiSSID, wifiPASS);
  lcd.clear(); lcd.setCursor(0,0); lcd.print("WLAN verbinden..");
  int tries=0;
  while(WiFi.status()!=WL_CONNECTED && tries<40) {
    delay(500); tries++;
    lcd.setCursor(tries%16, 1); lcd.print('.');
  }
  if(WiFi.status()==WL_CONNECTED) {
    wifiON=true;
    lcd.clear(); lcd.setCursor(0,0); lcd.print(T("WLAN verbunden!","WiFi connected!"));
    String ip=WiFi.localIP().toString();
    lcd.setCursor(0,1); lcd.print(ip);
    sndOk(); delay(2000);
    return true;
  }
  WiFi.disconnect(true);
  lcd.clear(); lcd.setCursor(0,0); lcd.print(T("WLAN Fehler!","WiFi failed!"));
  lcd.setCursor(0,1); lcd.print("PW pruefen!");
  sndErr(); delay(2000);
  return false;
}

// ── OTA Setup ────────────────────────────────────────────────
void otaSetup() {
  ArduinoOTA.setHostname("SpaceOS");
  ArduinoOTA.onStart([](){
    lcd.clear(); lcd.setCursor(0,0); lcd.print("OTA Update...");
    lcd.setCursor(0,1); lcd.print("[                ]");
  });
  ArduinoOTA.onProgress([](unsigned int prog, unsigned int total){
    int p=prog*16/total;
    lcd.setCursor(0,0); lcd.print("OTA Update...");
    for(int i=0;i<p;i++){lcd.setCursor(i+1,1);lcd.print('#');}
  });
  ArduinoOTA.onEnd([](){
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Update fertig!");
    lcd.setCursor(0,1); lcd.print(T("Neustart...","Restarting..."));
    sndOk(); delay(1500);
  });
  ArduinoOTA.begin();
}

// ── ChatGPT ──────────────────────────────────────────────────
void chatGPTKeyLoad() {
  prefs.begin("gpt", true);
  chatGPTKey = prefs.getString("key","");
  prefs.end();
}

String chatGPTAsk(const char* prompt) {
  if(!wifiON || chatGPTKey.length()<10) return T("Kein API Key!","No API Key!");
  HTTPClient http;
  http.begin("https://api.openai.com/v1/chat/completions");
  http.addHeader("Content-Type","application/json");
  http.addHeader("Authorization","Bearer "+chatGPTKey);
  String body = "{\"model\":\"gpt-3.5-turbo\",\"messages\":[{\"role\":\"user\",\"content\":\"";
  body += String(prompt);
  body += "\"}],\"max_tokens\":80}";
  int code = http.POST(body);
  if(code!=200){http.end();return "Fehler: "+String(code);}
  String resp = http.getString();
  http.end();
  int idx=resp.indexOf("\"content\":\"");
  if(idx<0) return "Keine Antwort";
  idx+=11;
  int end=resp.indexOf("\"",idx);
  if(end<0) return resp.substring(idx,min((int)resp.length(),idx+60));
  return resp.substring(idx,min(end,idx+60));
}


void drawWifiScan() {
  lcd.clear();
  // Zeile 0: ausgewähltes Netz mit Pfeil
  String s = wifiNets[wifiScanSel];
  char buf[17]; s.toCharArray(buf,17);
  lcd.setCursor(0,0); lcd.print('>');
  for(int i=0;i<15&&buf[i];i++) lcd.print(buf[i]);
  // Zeile 1: nächstes Netz
  if(wifiNetCount>1){
    int n2=(wifiScanSel+1)%wifiNetCount;
    String s2=wifiNets[n2];
    char buf2[17]; s2.toCharArray(buf2,17);
    lcd.setCursor(0,1); lcd.print(' ');
    for(int i=0;i<15&&buf2[i];i++) lcd.print(buf2[i]);
  }
}

// ── Boot & Shutdown ──────────────────────────────────────────
void doBoot(bool diag) {
  lcd.backlight();
  digitalWrite(PIN_GREEN,HIGH);
  sndBoot(); delay(100);
  lcd.clear(); lMid(0,"HW Electrics"); lMid(1,"\"SpaceOS\""); delay(2000);
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Memory...");
  lcd.setCursor(0,1); lcd.print("RAM...");
  for(int i=0;i<6;i++) {
    digitalWrite(PIN_WHITE,HIGH); delay(250);
    digitalWrite(PIN_WHITE,LOW);  delay(250);
  }
  delay(300);

  if(diag) {
    lcd.clear(); lMid(0,"Automatische"); lMid(1,"Diagnose..."); delay(2000);
    goDiagMenu(); return;
  }

  if(!pSetupDone()) { goLangSel(); }
  else { pLoad(); goLoginN(); }
}

void doOff() {
  sndShutdown(); lcd.clear(); lMid(0,T("Ausschalten...","Shutting down...")); delay(2000);
  lcd.clear(); lcd.noBacklight(); digitalWrite(PIN_GREEN,LOW);
  while(true) {
    if(digitalRead(PIN_BTN)==LOW) {
      delay(200); while(digitalRead(PIN_BTN)==LOW) delay(10);
      delay(200); doBoot(false); return;
    }
  }
}

void doRestart() {
  sndShutdown(); lcd.clear(); lMid(0,T("Neustart...","Restarting...")); delay(1500);
  ESP.restart();
}

void doForceOff() {
  btnH=false; btn3=false;
  lcd.clear(); lMid(0,T("Notaus...","Force off...")); delay(1000);
  lcd.clear(); lcd.noBacklight(); digitalWrite(PIN_GREEN,LOW);
  while(true) {
    if(digitalRead(PIN_BTN)==LOW) {
      delay(200); while(digitalRead(PIN_BTN)==LOW) delay(10);
      delay(200);
      int fc=pForceCount();
      doBoot(fc>=3); return;
    }
  }
}

// ── Button Handler ───────────────────────────────────────────
void handleBtn() {
  bool pr=(digitalRead(PIN_BTN)==LOW);
  if(pr&&!btnH) { btnH=true; btnT=millis(); btn3=false; }
  if(!pr) { btnH=false; btn3=false; }
  if(btnH) {
    unsigned long h=millis()-btnT;
    if(h>=5000) {
      int fc=pForceCount(); fc++; pSetForce(fc);
      doForceOff();
    } else if(h>=3000&&!btn3) {
      btn3=true;
      if(S==ST_DESKTOP||S==ST_APPMENU||S==ST_NOTES||S==ST_NOTESMENU||
         S==ST_CLOCK||S==ST_CALC||S==ST_SETTINGS||S==ST_SNAKE)
        goShutMenu();
    }
  }
}

// ── D Langdruck 5 Sek ────────────────────────────────────────
void checkDLong(char key) {
  if(key=='D') { if(!dLH){dLH=true;dLT=millis();dLDn=false;} }
  else if(key!=0) { dLH=false; dLDn=false; }
}

void checkDHeld() {
  if(dLH && !keypad.isPressed('D')) { dLH=false; dLDn=false; }
}

void updateDLong() {
  if(dLH&&!dLDn&&(millis()-dLT)>=5000) {
    dLDn=true;
    if(S==ST_NOTES||S==ST_NOTESMENU||S==ST_NOTESOPEN||
       S==ST_CLOCK||S==ST_CALC||S==ST_APPMENU||
       S==ST_SETTINGS||S==ST_SNAKE||S==ST_SNAKE_SCORE) {
      sndClick(); goDesk();
    }
  }
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  delay(1000);
  pinMode(PIN_GREEN,OUTPUT); pinMode(PIN_WHITE,OUTPUT);
  pinMode(PIN_BUZZ,OUTPUT);  pinMode(PIN_BTN,INPUT_PULLUP);
  digitalWrite(PIN_GREEN,LOW); digitalWrite(PIN_WHITE,LOW);

  Wire.begin(21,22);
  delay(300);
  lcd.init();
  delay(300);
  lcd.backlight();
  delay(200);
  lcd.clear();

  if(rtc.begin()) {
    rtcOK=true;
    if(!rtc.isrunning()) rtc.adjust(DateTime(2024,1,1,0,0,0));
  }

  // SD mit kurzer Verzögerung initialisieren
  delay(200);
  SPI.begin(18, 19, 23, PIN_SD_CS);
  if(SD.begin(PIN_SD_CS)) {
    sdOK=true;
    if(!SD.exists("/Notizen")) { SD.mkdir("/Notizen"); }
    if(!SD.exists("/Updates")) { SD.mkdir("/Updates"); }
  }

  pLoad();
  wifiLoad();
  chatGPTKeyLoad();
  randomSeed(analogRead(34));

  // WiFi verbinden wenn gespeichert
  if(strlen(wifiSSID)>0) wifiConnect();
  if(wifiON) otaSetup();

  int fc=pForceCount();
  doBoot(fc>=3);
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  handleBtn();
  if(wifiON) ArduinoOTA.handle();
  char key=keypad.getKey();
  if(!key) key=0;
  checkDLong(key);
  checkDHeld();
  updateDLong();


  // SPRACHE / LANGUAGE SELECTION
  if(S==ST_LANG_SEL){
    if(key=='A'||key=='B'){
      gLang=1-gLang;
      lcd.clear();
      lcd.setCursor(0,0); lcd.print(gLang==0?">Deutsch":" Deutsch");
      lcd.setCursor(0,1); lcd.print(gLang==1?">English":" English");
      sndClick();
    }
    if(key=='D'){
      sndOk();
      prefs.begin("spaceos", false);
      prefs.putInt("lang", gLang);
      prefs.end();
      if(pSetupDone()) { goSettings(); }
      else { goSetupN(); }
    }
    if(key=='C'){
      sndClick();
      if(pSetupDone()) goSettings();
    }
    return;
  }

  // SETUP NAME
  if(S==ST_SETUP_NAME){
    bool u=false;
    if(key){tKey(0,key);u=true;}
    if(tTick(0))u=true;
    if(u)showField(1,0);
    if(key=='D'){tGet(0,gU);if(strlen(gU)>=1){sndClick();goSetupP();}}
    return;
  }

  // SETUP PASS
  if(S==ST_SETUP_PASS){
    if(key){tKey(0,key);showStars(1,tLen[0]);}
    if(key=='D'){
      if(tLen[0]>=4){sndOk();tGet(0,gP);goSetupD();}
      else{sndErr();lRow(0);lcd.print(T("Mind. 4 Zeichen!","Min. 4 digits!"));delay(1500);lRow(0);lcd.print(T("Passwort:","Password:"));showStars(1,tLen[0]);}
    }
    return;
  }

  // SETUP DATUM
  if(S==ST_SETUP_DATE){
    if(key>='0'&&key<='9'&&tLen[0]<8){tKey(0,key);drawDateIn();}
    else if(key=='*'){tKey(0,key);drawDateIn();}
    else if(key=='D'&&tLen[0]==8){
      char* b=tBuf[0];
      int dd=(b[0]-'0')*10+(b[1]-'0'),mo=(b[2]-'0')*10+(b[3]-'0');
      int yy=(b[4]-'0')*1000+(b[5]-'0')*100+(b[6]-'0')*10+(b[7]-'0');
      if(rtcOK) rtc.adjust(DateTime(yy,mo,dd,12,0,0));
      sndClick(); goSetupT();
    }
    return;
  }

  // SETUP ZEIT
  if(S==ST_SETUP_TIME){
    if(key>='0'&&key<='9'&&tLen[0]<6){tKey(0,key);drawTimeIn();}
    else if(key=='*'){tKey(0,key);drawTimeIn();}
    else if(key=='D'&&tLen[0]==6){
      char* b=tBuf[0];
      int hh=(b[0]-'0')*10+(b[1]-'0'),mi=(b[2]-'0')*10+(b[3]-'0'),ss=(b[4]-'0')*10+(b[5]-'0');
      if(rtcOK){
        DateTime now=rtc.now();
        rtc.adjust(DateTime(now.year(),now.month(),now.day(),hh,mi,ss));
      }
      pSave();
      sndOk(); lSet(T("Einrichtung","Setup"),T("abgeschlossen!","complete!")); delay(2000);
      goDesk();
    }
    return;
  }

  // LOGIN NAME
  if(S==ST_LOGIN_NAME){
    bool u=false;
    if(key){tKey(1,key);u=true;}
    if(tTick(1))u=true;
    if(u)showField(1,1);
    if(key=='D'){
      char e[17]; tGet(1,e);
      if(strcmp(e,gU)==0){sndClick();goLoginP();}
      else{sndErr();lSet(T("Falscher Name!","Wrong Username!"),"");delay(1500);tReset(1,false);lSet(T("Benutzername:","Username:"),"");showField(1,1);}
    }
    return;
  }

  // LOGIN PASS
  if(S==ST_LOGIN_PASS){
    if(key){tKey(1,key);showStars(1,tLen[1]);}
    if(key=='D'){
      char e[7]; tGet(1,e);
      if(strcmp(e,gP)==0){
        sndOk();
        lcd.clear(); lMid(0,T("Laden...","Loading..."));
        for(int i=0;i<16;i++){lcd.setCursor(i,1);lcd.print('#');delay(80);}
        checkSDUpdate();
        goDesk();
      } else {
        sndErr(); lSet(T("Falsches PW!","Wrong Password!"),""); delay(1500);
        tReset(1,true); lSet(T(T("Passwort:","Password:"),"Password:"),""); showStars(1,0);
      }
    }
    return;
  }

  // DESKTOP
  if(S==ST_DESKTOP){
    if(millis()-deskT>1000){deskT=millis();char tb[9];tStr(tb);lRow(0);lcd.print(tb);}
    if(key=='A'){sndClick();goAppMenu();}
    return;
  }

  // APP MENÜ
  if(S==ST_APPMENU){
    if(key=='A'){appSel=(appSel+7)%8;drawApp();}
    if(key=='B'){appSel=(appSel+1)%8;drawApp();}
    if(key=='C'){sndClick();goDesk();}
    if(key=='D'){
      sndClick();
      if(appSel==0)goNotes();
      else if(appSel==1)goClock();
      else if(appSel==2)goCalc();
      else if(appSel==3)goSettings();
      else if(appSel==4) goSnake();
      else if(appSel==5){
        if(!wifiON){lSet(T("Kein WLAN!","No WiFi!"),T("Erst WLAN setup","Setup WiFi first"));sndErr();delay(2000);goAppMenu();}
        else{S=ST_CHATGPT;tReset(2,false);lcd.clear();lcd.setCursor(0,0);lcd.print(T("ChatGPT:","ChatGPT:"));showField(1,2);}
      }
      else if(appSel==6) goStopwatch();
      else if(appSel==7) goTimer();
    }
    return;
  }

  // SHUTDOWN MENÜ
  if(S==ST_SHUTMENU){
    if(key=='A'||key=='B'){shutSel=(shutSel+1)%2;drawShut();}
    if(key=='C'){sndClick();goDesk();}
    if(key=='D'){if(shutSel==0)doOff();else doRestart();}
    return;
  }

  // EINSTELLUNGEN
  if(S==ST_SETTINGS){
    if(key=='A'){setSel=(setSel+SET_COUNT-1)%SET_COUNT;drawSet();}
    if(key=='B'){setSel=(setSel+1)%SET_COUNT;drawSet();}
    if(key=='C'){sndClick();goDesk();return;}
    if(key=='D'){
      sndClick();
      if(setSel==0){
        S=ST_SET_DATE;tReset(0,true);
        lcd.clear();lcd.setCursor(0,0);lcd.print(T("Datum:TT.MM.JJJJ","Date:DD.MM.YYYY"));lcd.setCursor(0,1);lcd.print("__.__.____");
      }
      else if(setSel==1){S=ST_SET_CHPW_OLD;tReset(4,true);lSet(T("Altes Passwort:","Old Password:"),"");showStars(1,0);}
      else if(setSel==2){S=ST_SET_CHUN_OLD;tReset(4,true);lSet("Passwort best.:","");showStars(1,0);}
      else if(setSel==3){S=ST_DIAG_PW;tReset(4,true);lSet(T("Passwort:","Password:"),"");showStars(1,0);}
      else if(setSel==4){S=ST_SET_RESET;lSet(T("Alles loeschen?","Delete all?"),"D=Ja   C=Nein");}
      else if(setSel==5){
        S=ST_SET_VOL;
        lcd.clear();lcd.setCursor(0,0);lcd.print(T("Lautstaerke:","Volume:"));
        const char* vn[4]={T("Aus","Off"),T("Leise","Low"),T("Mittel","Med"),T("Laut","Loud")};
        lcd.setCursor(0,1);
        for(int i=0;i<4;i++){lcd.print(i==gVol?'>':' ');lcd.print(vn[i]);if(i<3)lcd.print(' ');}
      }
      else if(setSel==6){
        lcd.clear(); lMid(0,T("Suche Netze...","Scanning WiFi..."));
        lRow(1); lcd.print(T("Bitte warten...","Please wait..."));
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);
        int n=WiFi.scanNetworks(false, true);
        wifiNetCount=min(n,WIFI_MAX_NETS);
        for(int i=0;i<wifiNetCount;i++) wifiNets[i]=WiFi.SSID(i);
        WiFi.scanDelete();
        if(wifiNetCount==0){
          lSet(T("Keine Netze!","No networks!"),"C=Zurueck");
          sndErr(); delay(2000); goSettings(); return;
        }
        wifiScanSel=0;
        S=ST_WIFI_SCAN;
        drawWifiScan();
      }
      else if(setSel==7){
        S=ST_OTA_MODE;
        lcd.clear();
        if(wifiON){
          lcd.setCursor(0,0);lcd.print(T("OTA bereit!","OTA ready!"));
          String ip=WiFi.localIP().toString();
          lcd.setCursor(0,1);lcd.print(ip+":3232");
        } else {
          lcd.setCursor(0,0);lcd.print(T("Kein WLAN!","No WiFi!"));
          lcd.setCursor(0,1);lcd.print(T("Erst WLAN setup","Setup WiFi first"));
        }
        delay(3000); goSettings();
      }
      else if(setSel==9){
        S=ST_LANG_SEL;
        lcd.clear();
        lcd.setCursor(0,0); lcd.print(gLang==0?">Deutsch":" Deutsch");
        lcd.setCursor(0,1); lcd.print(gLang==1?">English":" English");
      }      else if(setSel==8){
        S=ST_API_KEY; tReset(0,false);
        lcd.clear();
        lcd.setCursor(0,0);lcd.print(T("API Key:","API Key:"));
        if(chatGPTKey.length()>0){
          lcd.setCursor(0,1);
          String preview="sk-..."+chatGPTKey.substring(chatGPTKey.length()-4);
          lcd.print(preview);
          delay(1500);
        }
        lRow(1); lcd.print("_");
      }
      else goDesk();
    }
    return;
  }

  // SET DATUM
  if(S==ST_SET_DATE){
    if(key>='0'&&key<='9'&&tLen[0]<8){tKey(0,key);drawDateIn();}
    else if(key=='*'){tKey(0,key);drawDateIn();}
    else if(key=='C'){sndClick();goSettings();return;}
    else if(key=='D'&&tLen[0]==8){
      char* b=tBuf[0];
      int dd=(b[0]-'0')*10+(b[1]-'0'),mo=(b[2]-'0')*10+(b[3]-'0');
      int yy=(b[4]-'0')*1000+(b[5]-'0')*100+(b[6]-'0')*10+(b[7]-'0');
      S=ST_SET_TIME;tReset(0,true);
      lcd.clear();lcd.setCursor(0,0);lcd.print(T("Zeit: HH:MM:SS","Time: HH:MM:SS"));lcd.setCursor(0,1);lcd.print("__:__:__");
      if(rtcOK){DateTime now=rtc.now();rtc.adjust(DateTime(yy,mo,dd,now.hour(),now.minute(),now.second()));}
    }
    return;
  }

  // SET ZEIT
  if(S==ST_SET_TIME){
    if(key>='0'&&key<='9'&&tLen[0]<6){tKey(0,key);drawTimeIn();}
    else if(key=='*'){tKey(0,key);drawTimeIn();}
    else if(key=='C'){sndClick();goSettings();return;}
    else if(key=='D'&&tLen[0]==6){
      char* b=tBuf[0];
      int hh=(b[0]-'0')*10+(b[1]-'0'),mi=(b[2]-'0')*10+(b[3]-'0'),ss=(b[4]-'0')*10+(b[5]-'0');
      if(rtcOK){DateTime now=rtc.now();rtc.adjust(DateTime(now.year(),now.month(),now.day(),hh,mi,ss));}
      sndOk();lSet(T("Gespeichert!","Saved!"),"");delay(1500);goSettings();
    }
    return;
  }

  // SET PW ALT
  if(S==ST_SET_CHPW_OLD){
    if(key){tKey(4,key);showStars(1,tLen[4]);}
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      char e[7];tGet(4,e);
      if(strcmp(e,gP)==0){sndClick();S=ST_SET_CHPW_NEW;tReset(4,true);lSet(T("Neues Passwort:","New Password:"),"");showStars(1,0);}
      else{sndErr();lSet(T("Falsches PW!","Wrong Password!"),"");delay(1500);tReset(4,true);lSet(T("Altes Passwort:","Old Password:"),"");showStars(1,0);}
    }
    return;
  }

  // SET PW NEU
  if(S==ST_SET_CHPW_NEW){
    if(key){tKey(4,key);showStars(1,tLen[4]);}
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      if(tLen[4]>=4){tGet(4,gP);pSave();sndSave();lSet(T("PW geaendert!","PW changed!"),"");delay(1500);goSettings();}
      else{sndErr();lRow(0);lcd.print(T("Mind. 4 Zeichen!","Min. 4 digits!"));delay(1500);lSet(T("Neues Passwort:","New Password:"),"");showStars(1,0);}
    }
    return;
  }

  // SET UN ALT
  if(S==ST_SET_CHUN_OLD){
    if(key){tKey(4,key);showStars(1,tLen[4]);}
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      char e[7];tGet(4,e);
      if(strcmp(e,gP)==0){sndClick();S=ST_SET_CHUN_NEW;tReset(4,false);lSet(T("Neuer Name:","New Username:"),"");showField(1,4);}
      else{sndErr();lSet(T("Falsches PW!","Wrong Password!"),"");delay(1500);tReset(4,true);lSet("Passwort best.:","");showStars(1,0);}
    }
    return;
  }

  // SET UN NEU
  if(S==ST_SET_CHUN_NEW){
    bool u=false;
    if(key){tKey(4,key);u=true;}
    if(tTick(4))u=true;
    if(u)showField(1,4);
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){tGet(4,gU);pSave();sndSave();lSet(T("Name geaendert!","Name changed!"),"");delay(1500);goSettings();}
    return;
  }

  // SET VOL
  if(S==ST_SET_VOL){
    const char* vn[4]={T("Aus","Off"),T("Leise","Low"),T("Mittel","Med"),T("Laut","Loud")};
    if(key=='A'){gVol=(gVol+3)%4;}
    if(key=='B'){gVol=(gVol+1)%4;}
    if(key=='A'||key=='B'){
      lcd.clear();lcd.setCursor(0,0);lcd.print(T("Lautstaerke:","Volume:"));
      lcd.setCursor(0,1);
      for(int i=0;i<4;i++){lcd.print(i==gVol?'>':' ');lcd.print(vn[i]);if(i<3)lcd.print(' ');}
      if(gVol>0) buzzTone(659,60);
    }
    if(key=='D'||key=='C'){
      pSave(); sndOk();
      if(key=='D'){lcd.clear();lMid(0,T("Gespeichert!","Saved!"));delay(1000);}
      goSettings();
    }
    return;
  }

  // SET RESET
  if(S==ST_SET_RESET){
    if(key=='D'){lSet(T("Loeschen...","Deleting..."),"");pWipe();delay(2000);lSet(T("Fertig!","Done!"),T("Neustart...","Restarting..."));delay(1500);ESP.restart();}
    if(key=='C'){sndClick();goSettings();}
    return;
  }

  // DIAG PW
  if(S==ST_DIAG_PW){
    if(key){tKey(4,key);showStars(1,tLen[4]);}
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      char e[7];tGet(4,e);
      if(strcmp(e,gP)==0){sndOk();pSetForce(3);ESP.restart();}
      else{sndErr();lSet(T("Falsches PW!","Wrong Password!"),"");delay(1500);tReset(4,true);lSet(T("Passwort:","Password:"),"");showStars(1,0);}
    }
    return;
  }

  // STOPPUHR
  if(S==ST_STOPWATCH){
    static unsigned long swLast=0;
    if(swRunning && millis()-swLast>50){ swLast=millis(); swDraw(); }
    if(key=='A'){
      if(!swRunning){
        swRunning=true; swStart=millis()-swElapsed;
        sndClick(); swLast=millis(); swDraw();
      } else {
        swElapsed=millis()-swStart; swRunning=false;
        sndClick(); swDraw();
      }
    }
    if(key=='B' && swRunning){
      unsigned long snap=millis()-swStart+swElapsed;
      unsigned long m=(snap%3600000)/60000,s=(snap%60000)/1000,cs=(snap%1000)/10;
      char buf[17]; sprintf(buf,"Rd %02lu:%02lu.%02lu",m,s,cs);
      lRow(1); lcd.print(buf);
      sndClick();
    }
    if(key=='*' && !swRunning){
      swElapsed=0; swRunning=false; swDraw(); sndClick();
    }
    if(key=='C'||key=='D'){ swRunning=false; sndClick(); goDesk(); return; }
    return;
  }

  // COUNTDOWN-TIMER
  if(S==ST_TIMER){
    // Phase 1: Alarm laeuft (Zeit abgelaufen, wartet auf Tastendruck)
    if(tmFinished){
      tmAlarmStep();
      if(key){ sndClick(); tmStop(); goTimer(); return; }
      return;
    }
    // Phase 2: Timer laeuft, zaehlt runter
    if(tmRunning){
      if(millis()-tmLastTick>=1000){
        tmLastTick+=1000;
        tmRemainSec--;
        if(tmRemainSec<=0){
          tmRemainSec=0;
          tmRunning=false; tmFinished=true;
          tmBlinkT=millis(); tmBlinkOn=false;
          lcd.clear();
          lMid(0,T("Zeit abgelaufen!","Time's up!"));
          lMid(1,T("Taste = Beenden","Key = Dismiss"));
        } else {
          tmDrawRun();
        }
      }
      if(key=='A'){ tmRunning=false; sndClick(); tmDrawRun(); }
      if(key=='*'){ tmRunning=false; tmRemainSec=0; sndClick(); goTimer(); return; }
      if(key=='C'||key=='D'){ tmStop(); sndClick(); goDesk(); return; }
      return;
    }
    // Phase 3: pausierter Timer (Zeit gesetzt, nicht laufend)
    if(tmTotalSec>0){
      if(key=='A'){ tmRunning=true; tmLastTick=millis(); sndClick(); tmDrawRun(); }
      if(key=='*'){ tmTotalSec=0; tmRemainSec=0; sndClick(); goTimer(); return; }
      if(key=='C'||key=='D'){ tmStop(); sndClick(); goDesk(); return; }
      return;
    }
    // Phase 4: Eingabe der Zeit (MMSS)
    bool u=false;
    if(key>='0'&&key<='9'&&tLen[0]<4){ tKey(0,key); u=true; }
    else if(key=='*'){ tKey(0,key); u=true; }
    if(u) tmDrawInput();
    if(key=='A'||key=='D'){ tmStartFromInput(); }
    if(key=='C'){ sndClick(); goDesk(); return; }
    return;
  }

  // WETTER
  if(S==ST_WEATHER){
    if(key=='A'){ sndClick(); goWeather(); return; }
    if(key=='C'||key=='D'||key=='B'){ sndClick(); goDesk(); return; }
    return;
  }

  // NOTIZEN
  if(S==ST_NOTES){
    bool u=false;
    if(key>='0'&&key<='9'){tKey(2,key);u=true;}
    else if(key=='*'){
      if(tPK[2]){tPK[2]=0;tPI[2]=0;u=true;}
      else if(nCol>0){nCol--;nLines[nTot-1][nCol]=0;u=true;}
      else if(nTot>1){nTot--;nCol=strlen(nLines[nTot-1]);if(nView>0&&nView>=nTot-1)nView--;u=true;}
    }
    else if(key=='#'){
      if(tPK[2]!=0){
        tCommit(2); tMode[2]=(tMode[2]+1)%3;
        u=true;
      } else {
        nFlush();
        if(nTot<NL){nTot++;nCol=0;memset(nLines[nTot-1],0,NW+1);if(nView<nTot-2)nView=nTot-2;}
        u=true;
      }
    }
    else if(key=='A'){nFlush();if(nView>0){nView--;u=true;}}
    else if(key=='B'){nFlush();if(nView<nTot-1){nView++;u=true;}}
    else if(key=='C'){nFlush();goNoteMenu();return;}
    if(tTick(2)){if(tLen[2]>0){char c=tBuf[2][tLen[2]-1];tLen[2]--;tBuf[2][tLen[2]]=0;nAdd(c);}u=true;}
    if(u)nShow();
    if(key>='0'&&key<='9'){sndClick();digitalWrite(PIN_WHITE,HIGH);delay(30);digitalWrite(PIN_WHITE,LOW);}
    return;
  }

  // WLAN SCANNER
  if(S==ST_WIFI_SCAN){
    if(key=='A'||key=='B'){
      if(key=='A') wifiScanSel=(wifiScanSel+wifiNetCount-1)%wifiNetCount;
      else         wifiScanSel=(wifiScanSel+1)%wifiNetCount;
      sndClick();
      drawWifiScan();
    }
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      wifiNets[wifiScanSel].toCharArray(wifiSSID,33);
      S=ST_WIFI_PASS; tReset(0,false);
      lcd.clear();
      lcd.setCursor(0,0);lcd.print("Passwort fuer:");
      char ssidBuf[17]; wifiNets[wifiScanSel].toCharArray(ssidBuf,17);
      lcd.setCursor(0,1);
      for(int i=0;i<14&&ssidBuf[i];i++) lcd.print(ssidBuf[i]);
      delay(1200);
      showField(1,0);
    }
    return;
  }

  // WLAN PASSWORT
  if(S==ST_WIFI_PASS){
    bool u=false;
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      tCommit(0);
      delay(50);
      tGet(0,wifiPASS);
      lcd.clear();
      char dbg[17]; sprintf(dbg,"PW: %d Zeichen",(int)strlen(wifiPASS));
      lcd.setCursor(0,0); lcd.print(dbg);
      lcd.setCursor(0,1); lcd.print(T("Verbinde...","Connecting..."));
      delay(1500);
      wifiSave();
      sndOk();
      if(wifiConnect()) otaSetup();
      goSettings();
      return;
    }
    if(key>='0'&&key<='9'){tKey(0,key);u=true;}
    else if(key=='*'){tKey(0,key);u=true;}
    else if(key=='#'){tKey(0,key);u=true;}
    if(tTick(0))u=true;
    if(u)showField(1,0);
    return;
  }

  // WLAN MANUELLE SSID (Fallback)
  if(S==ST_WIFI_SSID){
    bool u=false;
    if(key){tKey(0,key);u=true;}
    if(tTick(0))u=true;
    if(u)showField(1,0);
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      tGet(0,wifiSSID);
      S=ST_WIFI_PASS; tReset(0,false);
      lcd.clear();lcd.setCursor(0,0);lcd.print("WLAN Passwort:");
      showField(1,0);
    }
    return;
  }

  // CHATGPT
  if(S==ST_CHATGPT){
    static String cgBuf="";
    static bool cgWaiting=false;
    if(!cgWaiting){
      bool u=false;
      if(key){tKey(2,key);u=true;}
      if(tTick(2))u=true;
      if(u)showField(1,2);
      if(key=='C'){sndClick();tReset(2,false);cgBuf="";goDesk();return;}
      if(key=='D'){
        char q[17];tGet(2,q);
        if(strlen(q)>0){
          cgWaiting=true;
          lcd.clear();lcd.setCursor(0,0);lcd.print(T("Frage ChatGPT..","Asking ChatGPT.."));
          lRow(1);
          String ans=chatGPTAsk(q);
          lcd.clear();
          lScroll(0,ans.c_str(),3);
          lRow(1);lcd.print("D=weiter C=Ende");
          tReset(2,false);cgBuf=ans;cgWaiting=false;
        }
      }
    }
    if(cgWaiting){
      if(key=='D'){cgWaiting=false;lcd.clear();lcd.setCursor(0,0);lcd.print("Frage:");showField(1,2);}
      if(key=='C'){sndClick();tReset(2,false);cgBuf="";goDesk();return;}
    }
    return;
  }

  // API KEY EINGABE
  if(S==ST_API_KEY){
    bool u=false;
    if(key){tKey(0,key);u=true;}
    if(tTick(0))u=true;
    if(u)showField(1,0);
    if(key=='C'){sndClick();goSettings();return;}
    if(key=='D'){
      char k[65]; tGet(0,k);
      if(strlen(k)>=10){
        chatGPTKey=String(k);
        prefs.begin("gpt",false);
        prefs.putString("key",chatGPTKey);
        prefs.end();
        sndSave();
        lSet(T("API Key","API Key"),T("gespeichert!","stored!"));
        delay(1500);
        goSettings();
      } else {
        sndErr();
        lRow(0);lcd.print(T("Mind. 10 Zeichen","Min. 10 chars"));
        delay(1500);
        lRow(0);lcd.print(T("API Key:","API Key:"));
        showField(1,0);
      }
    }
    return;
  }

  // NOTIZEN MENÜ
  if(S==ST_NOTESMENU){
    if(key=='A'){nmSel=(nmSel+2)%3;drawNM();}
    if(key=='B'){nmSel=(nmSel+1)%3;drawNM();}
    if(key=='C'){sndClick();S=ST_NOTES;nShow();}
    if(key=='D'){
      sndClick();
      if(nmSel==0){
        if(sdOK){
          S=ST_SAVE_WHERE;
          lcd.clear();
          lcd.setCursor(0,0);lcd.print(">Auf Arduino");
          lcd.setCursor(0,1);lcd.print(" Auf SD-Karte");
        } else {
          int cnt=nCount();
          nSave(cnt);
          char b[17];sprintf(b,"Text%d.txt",cnt+1);
          lSet(T("Gespeichert!","Saved!"),b);sndSave();
          digitalWrite(PIN_WHITE,HIGH);delay(500);digitalWrite(PIN_WHITE,LOW);
          delay(800);goNoteMenu();
        }
      }
      else if(nmSel==1)goNoteOpen();
      else goDesk();
    }
    return;
  }

  // SPEICHERN AUSWAHL
  if(S==ST_SAVE_WHERE){
    static int swSel=0;
    if(key=='A'||key=='B'){
      swSel=(swSel+1)%2;
      lcd.clear();
      if(swSel==0){lcd.setCursor(0,0);lcd.print(">Auf Arduino");lcd.setCursor(0,1);lcd.print(" Auf SD-Karte");}
      else{lcd.setCursor(0,0);lcd.print(" Auf Arduino");lcd.setCursor(0,1);lcd.print(">Auf SD-Karte");}
    }
    if(key=='C'){sndClick();swSel=0;goNoteMenu();return;}
    if(key=='D'){
      sndClick();
      int cnt=nCount();
      if(swSel==0){
        nSave(cnt);
        char b[17];sprintf(b,"Text%d.txt",cnt+1);
        lSet(T("Gespeichert!","Saved!"),b);
      } else {
        nSaveSD(cnt);
        char b[17];sprintf(b,"Text%d.txt SD",cnt+1);
        lSet(T("Auf SD gespeich","Saved to SD"),b);
      }
      sndSave();
      digitalWrite(PIN_WHITE,HIGH);delay(500);digitalWrite(PIN_WHITE,LOW);
      delay(800);swSel=0;goNoteMenu();
    }
    return;
  }

  // NOTIZEN ÖFFNEN
  if(S==ST_NOTESOPEN){
    int cnt=nCount();
    if(key=='A'){noSel=(noSel+cnt-1)%cnt;drawNO();}
    if(key=='B'){noSel=(noSel+1)%cnt;drawNO();}
    if(key=='C'){sndClick();goNoteMenu();}
    if(key=='D'){sndClick();nLoad(noSel);tReset(2,false);S=ST_NOTES;nShow();}
    return;
  }

  // UHR
  if(S==ST_CLOCK){
    if(millis()-clkT>1000){clkT=millis();char t[9],d[11];tStr(t);dStr(d);lRow(0);lcd.print(t);lRow(1);lcd.print(d);}
    if(key=='C'||key=='B'){sndClick();goDesk();}
    return;
  }

  // RECHNER
  if(S==ST_CALC){
    if(key>='0'&&key<='9'){
      if(cBLen<14){
        if(cBuf[0]=='0'&&cBLen==1){cBuf[0]=key;cBLen=1;}
        else{cBuf[cBLen++]=key;}
        cBuf[cBLen]=0;lRow(0);lcd.print(cBuf);if(cOp){lcd.setCursor(15,0);lcd.print(cOp);}
      }
    }
    else if((key=='A'||key=='B'||key=='#')||(key=='*'&&!cHas)){
      cA=atol(cBuf);cOp=(key=='A'?'+':key=='B'?'-':key=='#'?'/':'*');
      cHas=1;strcpy(cBuf,"0");cBLen=1;lRow(0);lcd.print("0");lcd.setCursor(15,0);lcd.print(cOp);
    }
    else if(key=='*'&&cHas){
      if(cBLen>1)cBuf[--cBLen]=0;else{strcpy(cBuf,"0");cBLen=1;}
      lRow(0);lcd.print(cBuf);lcd.setCursor(15,0);lcd.print(cOp);
    }
    else if(key=='C'){sndClick();goDesk();}
    else if(key=='D'&&cHas){
      cB=atol(cBuf);long r=0;
      if(cOp=='+'||cOp=='-'||cOp=='*'||(cOp=='/'&&cB!=0)){
        if(cOp=='+')r=cA+cB;else if(cOp=='-')r=cA-cB;
        else if(cOp=='*')r=cA*cB;else r=cA/cB;
        sprintf(cBuf,"%ld",r);cBLen=strlen(cBuf);cOp=0;cHas=0;
        sndOk();lRow(0);lcd.print(cBuf);
      } else {sndErr();lRow(0);lcd.print("Div durch 0!");delay(1500);lRow(0);lcd.print(cBuf);}
    }
    return;
  }

  // SNAKE
  if(S==ST_SNAKE){
    if(key=='A'&&snDir!=0)snDir=2;
    if(key=='B'&&snDir!=2)snDir=0;
    if(key=='C'&&snDir!=1)snDir=3;
    if(key=='#'&&snDir!=3)snDir=1;
    if(key=='*'||key=='C'){sndClick();goDesk();return;}
    if(millis()-snT>=SN_SPEED){
      snT=millis();
      if(snMove()){
        sndGameOver();
        lcd.clear();
        char sb[17];sprintf(sb,"Score: %d",snScore);
        lMid(0,T("Game Over!","Game Over!"));lMid(1,sb);delay(2000);
        S=ST_SNAKE_NAME;tReset(1,false);
        lcd.clear();lcd.setCursor(0,0);lcd.print(T("Dein Name:","Your Name:"));
        showField(1,1);
        return;
      }
      snDraw();
    }
    return;
  }

  // SNAKE NAME
  if(S==ST_SNAKE_NAME){
    bool u=false;
    if(key){tKey(1,key);u=true;}
    if(tTick(1))u=true;
    if(u)showField(1,1);
    if(key=='D'){
      char nm[17];tGet(1,nm);
      if(strlen(nm)<1)strcpy(nm,gU);
      snSaveScore(nm);sndOk();
      goSnakeScore();
    }
    return;
  }

  // SNAKE SCORE
  if(S==ST_SNAKE_SCORE){
    if(key=='D'||key=='C'||key=='*'){sndClick();goSnake();}
    return;
  }

  // DIAGNOSE MENÜ
  if(S==ST_DIAGMENU){
    if(key=='A'){diagSel=(diagSel+2)%3;drawDiag();}
    if(key=='B'){diagSel=(diagSel+1)%3;drawDiag();}
    if(key=='D'){
      sndClick();
      if(diagSel==0){pResetForce();pLoad();goLoginN();}
      else if(diagSel==1){S=ST_FORGOT_N;tReset(3,false);lSet(T("Neuer Name:","New Username:"),"");showField(1,3);}
      else{S=ST_DIAGRESET;lSet(T("Alles loeschen?","Delete all?"),"D=Ja   C=Nein");}
    }
    return;
  }

  // DIAG RESET
  if(S==ST_DIAGRESET){
    if(key=='D'){lSet(T("Loeschen...","Deleting..."),"");pWipe();delay(2000);lSet(T("Fertig!","Done!"),T("Neustart...","Restarting..."));delay(1500);ESP.restart();}
    if(key=='C'){sndClick();goDiagMenu();}
    return;
  }

  // FORGOT NAME
  if(S==ST_FORGOT_N){
    bool u=false;
    if(key){tKey(3,key);u=true;}
    if(tTick(3))u=true;
    if(u)showField(1,3);
    if(key=='D'){
      tGet(3,forgN);
      if(strlen(forgN)>=1){sndClick();S=ST_FORGOT_P;tReset(3,true);lSet(T("Neues Passwort:","New Password:"),"");showStars(1,0);}
    }
    return;
  }

  // FORGOT PASS
  if(S==ST_FORGOT_P){
    if(key){tKey(3,key);showStars(1,tLen[3]);}
    if(key=='D'){
      if(tLen[3]>=4){
        char np[7];tGet(3,np);
        strcpy(gU,forgN);strcpy(gP,np);
        pSave();pResetForce();
        sndSave();lSet(T("Gespeichert!","Saved!"),"Starte neu...");delay(2000);goLoginN();
      } else {
        sndErr();lRow(0);lcd.print(T("Mind. 4 Zeichen!","Min. 4 digits!"));delay(1500);
        lSet(T("Neues Passwort:","New Password:"),"");showStars(1,tLen[3]);
      }
    }
    return;
  }
}
