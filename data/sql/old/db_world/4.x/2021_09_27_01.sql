-- DB update 2021_09_27_00 -> 2021_09_27_01
DROP PROCEDURE IF EXISTS `updateDb`;
DELIMITER //
CREATE PROCEDURE updateDb ()
proc:BEGIN DECLARE OK VARCHAR(100) DEFAULT 'FALSE';
SELECT COUNT(*) INTO @COLEXISTS
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'version_db_world' AND COLUMN_NAME = '2021_09_27_00';
IF @COLEXISTS = 0 THEN LEAVE proc; END IF;
START TRANSACTION;
ALTER TABLE version_db_world CHANGE COLUMN 2021_09_27_00 2021_09_27_01 bit;
SELECT sql_rev INTO OK FROM version_db_world WHERE sql_rev = '1632235858441614200'; IF OK <> 'FALSE' THEN LEAVE proc; END IF;
--
-- START UPDATING QUERIES
--

INSERT INTO `version_db_world` (`sql_rev`) VALUES ('1632235858441614200');

UPDATE 
	`creature_loot_template` 
SET 
	`Chance` = 
	(CASE 
		WHEN `Entry` = 7438 THEN 7
		WHEN `Entry` = 7439 THEN 7
		WHEN `Entry` = 7440 THEN 8
		WHEN `Entry` = 7441 THEN 8
		WHEN `Entry` = 7442 THEN 8
		WHEN `Entry` = 10199 THEN 7
		WHEN `Entry` = 10916 THEN 6
	END)
WHERE
	`Item` = 12820
AND
	`Entry` IN (7438, 7439, 7440, 7441, 7442, 10199, 10916);

--
-- END UPDATING QUERIES
--
UPDATE version_db_world SET date = '2021_09_27_01' WHERE sql_rev = '1632235858441614200';
COMMIT;
END //
DELIMITER ;
CALL updateDb();
DROP PROCEDURE IF EXISTS `updateDb`;