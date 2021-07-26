CREATE PROCEDURE twt_tag_ignore( @tag varchar(255), @count int )
as
	declare @existing int;
	set @existing=(select id from twt_tags where tag=@tag);
	if( @existing is null )
		insert into twt_tags(tag,ignored_count) values( @tag, @count );
	else
		update twt_tags set ignored_count=@count where id=@existing;
