CREATE DATABASE `logdb`;


CREATE TABLE `logdb`.`monitorlog_template` 
(
  `no` BIGINT NOT NULL AUTO_INCREMENT,
  `logtime`		DATETIME,
  `serverno`	INT NOT NULL,  -- 서버마다의 고유번호 각자 지정
  `type`		INT NOT NULL,
  `avr`      INT NOT NULL DEFAULT 0,
  `min`      INT NOT NULL DEFAULT 0,
  `max`      INT NOT NULL DEFAULT 0,
  
PRIMARY KEY (`no`) );





