DELETE FROM `spell_bonus_data` WHERE `entry` = 50536;
INSERT INTO `spell_bonus_data` VALUES
(50536,0,0,0,'Death Knight - Unholy Blight Triggered');

DELETE FROM `spell_proc_event` WHERE `entry` = 49194;
INSERT INTO `spell_proc_event` VALUES
(49194,0x00,15,0x00002000,0x00000000,0x00000000,0x00000000,0x00000000,0.000000,0.000000,0);
