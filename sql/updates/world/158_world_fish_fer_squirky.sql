DELETE FROM `conditions` WHERE `SourceEntry` = '99431';
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorType`, `ScriptName`, `Comment`) VALUES
(13, 1, 99431, 0, 0, 31, 0, 3, 53544, 0, 0, 0, '', 'Targeting -> Squirky');