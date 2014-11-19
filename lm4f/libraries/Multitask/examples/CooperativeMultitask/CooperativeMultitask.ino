#include <Multitask.h>

/* modified delay to call cmt_yeld() */
void delay_sleep(unsigned long interval)
{
  unsigned long start = millis();
  while(millis() - start < interval)
    cmt_yeld();
}

void loop1()
{
  int led = D1_LED;
  int timeout = 30;
  pinMode(led, OUTPUT);
  Serial.println("[loop1 BEGIN]");
  while(--timeout)
  {
    digitalWrite(led, HIGH);
    delay_sleep(1000);
    digitalWrite(led, LOW);
    delay_sleep(1000);
  }
  Serial.println("[loop1 END]");
}

void loop2()
{
  int led = D2_LED;
  int timeout = 40;
  pinMode(led, OUTPUT);
  Serial.println("[loop2 BEGIN]");
  while(--timeout)
  {
    digitalWrite(led, HIGH);
    delay_sleep(300);
    digitalWrite(led, LOW);
    delay_sleep(300);
  }
  Serial.println("[loop2 END]");
}

CooperativeScheduler tasker;

void setup()
{
  Serial.begin(115200);
  Serial.println("Serial console initialized");
  
  tasker.begin();
  tasker.create_task(loop1);
  tasker.create_task(loop2);
  
  Serial.println("[run tasks BEGIN]");
  tasker.run();
  Serial.println("[run tasks END]");
}

void loop()
{
}
