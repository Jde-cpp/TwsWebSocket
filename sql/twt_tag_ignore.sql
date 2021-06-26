CREATE PROCEDURE twt_tag_ignore( _tag varchar(255), _count int unsigned )
begin
	declare _existing int unsigned;
	select id into _existing from twt_tags where tag=_tag;
	if( _existing is null ) then
		insert into twt_tags(tag,ignored_count) values( _tag, _count );
	else
		update twt_tags set ignored_count=_count where id=_existing;
	end if;
end