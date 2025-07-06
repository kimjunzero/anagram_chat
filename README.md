# Raspberry Pi - anagram_chat/I2C_LCD Driver

![image.png](attachment:9371f34f-66a6-404f-bad3-5f43c5e2ff59:image.png)

### 1. 개요

- 본 프로젝트는 **애너그램 퀴즈를 TCP/IP 기반의 채팅**을 통해 실시간으로 즐길 수 있도록 설계된 게임 시스템.
- **서버-클라이언트 구조**로 구성되어 있으며, 참가자가 정답을 맞출 경우 해당 내용은 **LCD(I2C)** 디바이스에 표시된다.
- 커널 모듈 방식으로 직접 작성한 `i2c_lcd_driver.c`를 통해 LCD를 제어

---

### 2. 시스템 아키텍처 / 흐름도

### 📊 시스템 흐름

- 클라이언트(명령어 입력) → TCP 소켓 통신 → server(퀴즈 출제 및 응답 처리) → 퀴즈 정답 여부 판단 → LCD 출력(Kernel Module)

```c
[서버 실행]
   |
   └─▶ 1. socket() 소켓 생성
   └─▶ 2. bind()로 포트 지정
   └─▶ 3. listen()으로 대기 시작
   └─▶ 4. select()로 클라이언트 연결 감시

[클라이언트 실행]
   |
   └─▶ 1. socket() 생성
   └─▶ 2. connect()로 서버 접속 요청

[서버]
   |
   └─▶ 5. accept()로 새 클라이언트 수락
   └─▶ 6. 클라이언트 소켓 관리 배열에 등록
   └─▶ 7. LCD 출력 및 브로드캐스트

[클라이언트]
   |
   └─▶ 3. 송신/수신 쓰레드 생성
         └─▶ send_msg(): 사용자 입력을 서버로 보냄
         └─▶ recv_msg(): 서버 메시지를 받아서 출력

[서버]
   |
   └─▶ 8. select()로 메시지 수신 감시
   └─▶ 9. 클라이언트 요청 종류에 따라 분기:
         ├─ "!quiz": 퀴즈 출제
         ├─ "!score": 점수판 요청
         ├─ "!rank" : 순위 요청
         ├─ "!exit" : 종료 요청
         └─ 일반 메시지: 채팅 브로드캐스트

   └─▶ 10. LCD에 메시지 출력

[클라이언트 종료]
   |
   └─▶ 쓰레드 종료 + 소켓 닫기

[서버]
   |
   └─▶ 클라이언트 연결 해제 감지
       └─ 소켓 닫고 배열에서 제거
       └─ LCD 인원 수 업데이트

### 3. 핵심 기능

1. 클라이언트 다중 접속 처리(멀티 스레드 기반)
2. 실시간 퀴즈 출제 및 정답 확인
3. 퀴즈 정답자 이름 → LCD 출력
4. 랭킹 확인 명령 → LCD에 출력
5. 커널 모듈로 I2C LCD 직접 제어 

 **3. 1 결과 화면**

![image.png](attachment:77e40e8e-6536-448b-87e4-c8fb7ee1fd92:image.png)

(사진 넣기)

- 자동 모드

![image.png](attachment:f86a67ad-ef83-420e-9982-6038fa34f1b3:image.png)

- LCD 출력 화면
- 서버 시작
    
    ![image.png](attachment:acbf2ee6-e836-417b-90b2-3b4543952c25:image.png)
    
    ![image.png](attachment:acbf2ee6-e836-417b-90b2-3b4543952c25:image.png)
    
- 현재 Player 수
    
    ![image.png](attachment:a9bd381a-adeb-40a5-be0d-4c1a6505a4d0:image.png)
    
- 문제 출력
    
    ![image.png](attachment:0ae358aa-dc39-49a2-9fbb-47bc8257d80e:image.png)
    
- 문제 맞춘 경우
    
    ![image.png](attachment:2672d99e-18a1-4941-92c8-6afe025df558:image.png)
    
- 현재랭킹 : !rank
    
    ![image.png](attachment:536c6789-317b-4016-8407-0c00d74b3dd2:image.png)
    
- 현재 우승 후보 : !score, 맞춘 문제 수
    
    ![image.png](attachment:d76f17d7-810a-4882-bde1-60bac3729d09:image.png)
    

- 시연 영상
    - 수동 모드 게임
    
    [1.mp4](attachment:f09bea3d-367c-406a-8851-d6709e4addb2:1.mp4)
    
    - 자동 모드 게임
    
    [5.mp4](attachment:140184e5-7525-479c-b6bc-4bd8e38f0f1b:5.mp4)
    

---

### 4. 기술 스택

| 분류 | 기술 |
| --- | --- |
| 언어 | C(커널 및 시스템 프로그래밍) |
| 통신 | TCP.IP 소켓(멀티 클라이언트) |
| 커널 모듈 | I2C 프로토콜, LCD1602 |
| 플랫폼 | Raspberry Pi(aarch64) |

---

### 6. 고찰 및 개선 방향

- LCD 커서 이동 문제
    - `\n` 문자로 줄바꿈을 처리하려고 했으나, LCD는 직접 커서 주소를 지정해야 함
    - `\n` 입력 시 이상한 아스키 코드(0x0A)가 출력됨
    - **개선**: 줄별 시작 주소를 미리 정의하고, 줄바꿈 시 커서 주소를 직접 설정하는 함수로 처리
- 자동 문제 출제 코드 통합 문제
    - 수동/자동 모드로 코드 관리했어야하는데 하나로 합치지 못함, 함수 단위로 통합, 재사용 가능하도록 구조화 보완 필요
