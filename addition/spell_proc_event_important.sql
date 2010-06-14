DELETE FROM `spell_proc_event` WHERE `entry` IN (71562, 71519);
INSERT INTO `spell_proc_event` VALUES ('71562', '0', '0', '0', '0', '0', '0', '0', '1', '0', '0');
INSERT INTO `spell_proc_event` VALUES ('71519', '0', '0', '0', '0', '0', '0', '0', '1', '0', '0');
UPDATE `spell_proc_event` SET `SpellFamilyMask1` =  0x08000000 WHERE `entry` = 55666;
