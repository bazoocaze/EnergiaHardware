#include <Multitask.h>

void loop1()
{
  int led = D1_LED;
  int timeout = 30;
  pinMode(led, OUTPUT);
  Serial.println("[loop1 BEGIN]");
  while(--timeout)
  {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
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
    delay(300);
    digitalWrite(led, LOW);
    delay(300);
  }
  Serial.println("[loop2 END]");
}

PreemptiveScheduler tasker;

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
