CREATE SCHEMA restaurant AUTHORIZATION restaurant;

CREATE TABLE CHECK_DETAIL (
CHECK_NO INTEGER NOT NULL REFERENCES CHECK_HEADER ON DELETE CASCADE,
LINE_NO INTEGER NOT NULL,
MENU_ITEM_NO INTEGER REFERENCES MENU_ITEM,
QTY DECIMAL(5) DEFAULT 1,
PRIMARY KEY (CHECK_NO, LINE_NO) );

CREATE TABLE "SERVER" (
SERVER_NO INTEGER NOT NULL PRIMARY KEY,
SERVER_NAME VARCHAR(20),
PRIMARY KEY (SERVER_NO) );

CREATE TABLE MENU_ITEM (
MENU_ITEM_NO INTEGER NOT NULL,
MENU_ITEM_NAME VARCHAR(40),
MENU_ITEM_GROUP_NO INTEGER,
PRICE DECIMAL(6,2),
PRIMARY KEY (MENU_ITEM_NO),
FOREIGN KEY (MENU_ITEM_GROUP_NO)
REFERENCES MENU_GROUP (MENU_ITEM_GROUP_NO),
CHECK (PRICE>0) );

CREATE TABLE CHECK_HEADER (
CHECK_NO INTEGER NOT NULL,
SERVER_NO INTEGER REFERENCES "SERVER",
START_TIME TIMESTAMP(0),
PRIMARY KEY (CHECK_NO) );

INSERT INTO "SERVER" VALUES
(1,'John Smith'),
(2,'Mary Jones');

DELETE FROM menu_item WHERE menu_item_no=102;

--SET CONSTRAINTS ALL DEFERRED
--SET CONSTRAINTS ALL IMMEDIATE
/*
CREATE PROCEDURE setPrice ( item_no INTEGER,
new_price DECIMAL(6,2) )
BEGIN
UPDATE menu_item SET price = new_price WHERE menu_item_no = item_no;
INSERT INTO audit VALUES (CURRENT_TIMESTAMP, item_no, new_price);
END;
CALL setPrice ( 303, 1.95 );
*/

SELECT MENU_ITEM_NO, MENU_ITEM_NAME, MENU_ITEM_GROUP_NO, PRICE
FROM MENU_ITEM;

SELECT *
FROM MENU_ITEM;

SELECT *
FROM MENU_ITEM
WHERE PRICE>5.00;

SELECT *
FROM MENU_ITEM
WHERE PRICE>5.00
ORDER BY PRICE;

SELECT *
FROM MENU_ITEM
WHERE PRICE>5.00
ORDER BY PRICE DESC, MENU_ITEM_NAME;

SELECT *
FROM MENU_ITEM
WHERE
MENU_ITEM_GROUP_NO>=5
AND MENU_ITEM_GROUP_NO<=7
AND PRICE>2.00;

SELECT *
FROM MENU_ITEM
WHERE
MENU_ITEM_GROUP_NO=1
OR MENU_ITEM_GROUP_NO=2;

SELECT *
FROM MENU_ITEM
WHERE
(MENU_ITEM_GROUP_NO=1
OR MENU_ITEM_GROUP_NO=2)
AND MENU_ITEM_NAME NOT LIKE 'CHICKEN%';

SELECT *
FROM CHECK_DETAIL
WHERE (MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM CHECK_DETAIL JOIN CHECK_HEADER USING (CHECK_NO)
WHERE (MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM CHECK_DETAIL NATURAL JOIN CHECK_HEADER
WHERE (MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM
CHECK_DETAIL JOIN CHECK_HEADER ON (CHECK_DETAIL.CHECK_NO=CHECK_HEADER.CHECK_NO)
WHERE (MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM CHECK_DETAIL, CHECK_HEADER
WHERE
CHECK_DETAIL.CHECK_NO=CHECK_HEADER.CHECK_NO
AND
(MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM CHECK_DETAIL CROSS JOIN CHECK_HEADER
WHERE
CHECK_DETAIL.CHECK_NO=CHECK_HEADER.CHECK_NO
AND
(MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM CHECK_DETAIL JOIN CHECK_HEADER USING (CHECK_NO) JOIN "SERVER" USING (SERVER_NO)
WHERE (MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM CHECK_DETAIL NATURAL JOIN CHECK_HEADER NATURAL JOIN "SERVER"
WHERE (MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM
CHECK_DETAIL
JOIN CHECK_HEADER ON (CHECK_DETAIL.CHECK_NO=CHECK_HEADER.CHECK_NO)
JOIN "SERVER" ON (CHECK_HEADER.SERVER_NO="SERVER".SERVER_NO)
WHERE (MENU_ITEM_NO,QTY)=(304,2);

SELECT *
FROM
CHECK_DETAIL,
CHECK_HEADER,
"SERVER"
WHERE
CHECK_DETAIL.CHECK_NO=CHECK_HEADER.CHECK_NO
AND CHECK_HEADER.SERVER_NO="SERVER".SERVER_NO
AND (MENU_ITEM_NO,QTY)=(304,2);

SELECT MENU_ITEM_NO, MENU_ITEM_NAME, CHECK_DETAIL.*
FROM MENU_ITEM LEFT OUTER JOIN CHECK_DETAIL USING (MENU_ITEM_NO);

SELECT MENU_ITEM_NO, MENU_ITEM_NAME, CHECK_DETAIL.*
FROM CHECK_DETAIL RIGHT OUTER JOIN MENU_ITEM USING (MENU_ITEM_NO);

SELECT MENU_ITEM_NO, MENU_ITEM_NAME, CHECK_DETAIL.*
FROM MENU_ITEM NATURAL LEFT OUTER JOIN CHECK_DETAIL;

SELECT MENU_ITEM_GROUP_NAME, LOWER(MENU_ITEM_GROUP_NAME), UPPER(MENU_ITEM_GROUP_NAME)
FROM MENU_ITEM_GROUP;

SELECT SERVER_NO, CAST(SERVER_NO AS CHAR(3))
FROM "SERVER";

SELECT CAST(SERVER_NO AS CHAR(3)) || SERVER_NAME
FROM "SERVER";

SELECT
'['||SAMPLE||']',
'['||TRIM(TRAILING FROM SAMPLE)||']',
'['||TRIM(LEADING FROM SAMPLE)||']',
'['||TRIM(BOTH FROM SAMPLE)||']'
FROM
(
VALUES (' SPACE IS DEFAULT ')
) AS X (SAMPLE);

SELECT SAMPLE, TRIM(TRAILING '0' FROM SAMPLE), TRIM(LEADING '0' FROM SAMPLE),
TRIM(BOTH '0' FROM SAMPLE)
FROM
(
VALUES ('00005.00')
) AS X (SAMPLE);

SELECT MENU_ITEM_GROUP_NAME, POSITION('r' IN MENU_ITEM_GROUP_NAME)
FROM MENU_ITEM_GROUP;

SELECT MENU_ITEM_GROUP_NAME, SUBSTRING(MENU_ITEM_GROUP_NAME FROM 3),
SUBSTRING(MENU_ITEM_GROUP_NAME FROM 3 FOR 4)
FROM MENU_ITEM_GROUP;

SELECT MENU_ITEM_GROUP_NAME, CHARACTER_LENGTH(MENU_ITEM_GROUP_NAME)
FROM MENU_ITEM_GROUP;

SELECT MENU_ITEM_GROUP_NAME, OCTET_LENGTH(MENU_ITEM_GROUP_NAME)
FROM MENU_ITEM_GROUP;

ELECT
SERVER_NO,
CASE
WHEN COUNT(*)>20 THEN 'FAST'
WHEN COUNT(*)>10 THEN 'MEDIUM'
ELSE
'SLOW'
END AS SERVER_SPEED
FROM
CHECK_DETAIL JOIN CHECK_HEADER USING (CHECK_NO)
GROUP BY SERVER_NO;

SELECT
CHECK_NO,
START_TIME,
CASE SERVER_NO
WHEN 1 THEN 'ONE'
WHEN 2 THEN 'TWO'
ELSE
'?'
END AS "SERVER"
FROM CHECK_HEADER;

SELECT MAX(PRICE)
FROM MENU_ITEM;

SELECT MAX(PRICE) AS HIGHEST_PRICE
FROM MENU_ITEM;

SELECT
MAX(PRICE) AS HIGHEST_PRICE,
MIN(PRICE) AS LOWEST_PRICE,
AVG(PRICE) AS MEAN_PRICE,
SUM(PRICE) AS TOTAL_PRICE
FROM MENU_ITEM;

SELECT COUNT(*) AS NUMBER_OF_ROWS
FROM MENU_ITEM;

SELECT MENU_ITEM_GROUP_NO, AVG(PRICE) AS AVERAGE_PRICE
FROM MENU_ITEM
GROUP BY MENU_ITEM_GROUP_NO;

SELECT MENU_ITEM_GROUP_NO, MENU_ITEM_GROUP_NAME, AVG(PRICE) AS AVERAGE_PRICE
FROM MENU_ITEM JOIN MENU_ITEM_GROUP USING (MENU_ITEM_GROUP_NO)
GROUP BY MENU_ITEM_GROUP_NO, MENU_ITEM_GROUP_NAME;

SELECT MENU_ITEM_GROUP_NO, AVG(PRICE) AS AVERAGE_PRICE
FROM MENU_ITEM
GROUP BY MENU_ITEM_GROUP_NO
HAVING AVG(PRICE)>2.50;

SELECT MIN(PRICE)
FROM MENU_ITEM;

SELECT *
FROM MENU_ITEM
WHERE
PRICE=
(
SELECT MIN(PRICE)
FROM MENU_ITEM
);

SELECT
MENU_ITEM_NAME,
PRICE,
(SELECT AVG(PRICE) FROM MENU_ITEM) - PRICE AS DEVIATION
FROM MENU_ITEM
ORDER BY DEVIATION;

SELECT PRICE FROM MENU_ITEM WHERE MENU_ITEM_GROUP_NO=1

SELECT *
FROM MENU_ITEM
WHERE
PRICE > ANY
(
SELECT PRICE FROM MENU_ITEM WHERE MENU_ITEM_GROUP_NO=1
);

SELECT *
FROM MENU_ITEM
WHERE
PRICE > ALL
(
SELECT PRICE FROM MENU_ITEM WHERE MENU_ITEM_GROUP_NO=1
);

SELECT *
FROM MENU_ITEM
WHERE
PRICE IN
(
SELECT PRICE FROM MENU_ITEM WHERE MENU_ITEM_GROUP_NO=1
);

SELECT *
FROM MENU_ITEM
NATURAL JOIN
(
SELECT PRICE
FROM MENU_ITEM
WHERE MENU_ITEM_GROUP_NO=1
) AS SUBSELECT_TABLE;

SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1;

SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2;

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
UNION
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
UNION ALL
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
EXCEPT
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
EXCEPT ALL
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2)
EXCEPT
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
INTERSECT
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
INTERSECT ALL
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
INTERSECT ALL CORRESPONDING
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=1)
INTERSECT ALL CORRESPONDING BY (MENU_ITEM_NO)
(SELECT MENU_ITEM_NO FROM CHECK_DETAIL WHERE QTY=2);

SELECT
CHECK_NO, LINE_NO, MENU_ITEM_GROUP_NAME, MENU_ITEM_NAME, PRICE, QTY
FROM
"SERVER" NATURAL JOIN
CHECK_HEADER NATURAL JOIN
MENU_ITEM_GROUP NATURAL JOIN
MENU_ITEM NATURAL JOIN
CHECK_DETAIL
ORDER BY
CHECK_NO, LINE_NO;

SELECT
MENU_ITEM_NAME,
PRICE,
CASE PRICE
WHEN (SELECT MAX(PRICE) FROM MENU_ITEM) THEN 'MOST'
WHEN (SELECT MIN(PRICE) FROM MENU_ITEM) THEN 'LEAST'
ELSE
'IN BETWEEN'
END AS EXTREMITY
FROM
MENU_ITEM;

SELECT
MENU_ITEM_NAME,
PRICE,
CASE
WHEN PRICE < 2.00 THEN 'LOW'
WHEN PRICE > 6.00 THEN 'HIGH'
ELSE
'MEDIUM'
END AS PRICE_BAND
FROM
MENU_ITEM;

SELECT
*
FROM
MENU_ITEM
WHERE
MENU_ITEM_NAME LIKE 'CHICKEN%';

SELECT
*
FROM
MENU_ITEM
WHERE
MENU_ITEM_NO NOT IN (SELECT DISTINCT MENU_ITEM_NO FROM CHECK_DETAIL);

SELECT *
FROM
CHECK_DETAIL
WHERE
(MENU_ITEM_NO, QTY) = (108, 2);

SELECT *
FROM
MENU_ITEM
WHERE
PRICE IN (1.85, 1.95, 2.10)
ORDER BY PRICE;

SELECT
CHECK_NO, LINE_NO, MENU_ITEM_NAME, QTY, PRICE, QTY*PRICE AS EXTENSION
FROM
MENU_ITEM M CROSS JOIN CHECK_DETAIL D
WHERE
D.MENU_ITEM_NO=M.MENU_ITEM_NO;

SELECT
CHECK_NO, SUM(QTY*PRICE) AS CHECK_TOTAL
FROM
MENU_ITEM JOIN CHECK_DETAIL USING (MENU_ITEM_NO)
GROUP BY CHECK_NO;

SELECT *
FROM
MENU_ITEM
WHERE
(PRICE,MENU_ITEM_GROUP_NO) IN ( VALUES (1.95,3), (1.20,5) );

SELECT *
FROM MENU_ITEM
NATURAL JOIN
(
(SELECT * FROM MENU_ITEM)
EXCEPT CORRESPONDING
(SELECT * FROM CHECK_DETAIL)
) AS not_ordered;


commit;
