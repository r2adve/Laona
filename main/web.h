#pragma once
/*
 * 휴대폰용 웹서버 (같은 와이파이에서 http://laona.local 또는 http://<IP>)
 *   /          : 대화 페이지
 *   /settings  : API 키 등록 (비밀번호 보호)
 */
void web_start(void);   /* IP 를 받은 뒤 호출. 여러 번 불러도 한 번만 시작 */
void web_stop(void);    /* 와이파이 설정 모드가 80 포트를 쓰는 동안 */

/* 화면에 접속 주소 QR 을 보여줌 (와이파이가 안 되어 있으면 와이파이 설정 모드) */
void web_show_address(void);
