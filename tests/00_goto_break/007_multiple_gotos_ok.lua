success
function f() 
    goto titi

    if a == 12 then
        goto toto
    end

    ::toto::
    if a == 12 then
        local i = 0
        goto titi
    end
    ::titi::
    local a = 12

    return function()
        goto titi

        if a == 12 then
            for i = 1, 10, 2 do
                goto tutu
            end
        end

        ::tutu::
        repeat
            if a == 12 then
                goto tutu
            end
        until a == 13

        ::titi::
        return function()
            ::tutu::
            goto tutu
        end
    end
end
